#!/usr/bin/env python3
"""
Skill code generator: .skill.toml → .fbs + _gen.hpp + .cpp

Usage:
    python skill_generator.py <input.skill.toml> --outdir <dir> [--srcdir <dir>]

Outputs:
    <ClassName>.fbs        — FlatBuffers schema (fed to flatc)
    <ClassName>_gen.hpp    — Full skill class with all boilerplate
    <ClassName>.cpp        — Registration file (REGISTER_SKILL_CLASS)
"""

import argparse
import os
import sys
import textwrap

import tomllib

from field_types import FieldDef, get_role_handler

FBS_NAMESPACE = "TaskMessenger.Skills"
CPP_NAMESPACE = "TaskMessenger::Skills"


# =============================================================================
# TOML Parsing
# =============================================================================

def load_skill_def(path: str) -> dict:
    """Load and validate a .skill.toml file."""
    with open(path, "rb") as f:
        data = tomllib.load(f)

    # Basic validation
    for key in ("skill", "request", "response"):
        if key not in data:
            raise ValueError(f"Missing required top-level key: {key!r}")

    skill = data["skill"]
    for key in ("name", "class_name", "description", "version"):
        if key not in skill:
            raise ValueError(f"Missing required skill key: {key!r}")

    return data


def parse_fields(fields_raw: list[dict]) -> list[FieldDef]:
    """Parse field list from skill definition into FieldDef objects.

    Matrix fields with rows_dim/cols_dim are auto-expanded: synthetic dim
    role fields are injected, and rows_field/cols_field are set automatically.
    """
    result = []
    for f in fields_raw:
        d = dict(f)
        if "type" in d:
            d["fbs_type"] = d.pop("type")
        fd = FieldDef(**d)
        if fd.role == "matrix" and fd.rows_dim and fd.cols_dim:
            fd.rows_field = f"{fd.name}_rows"
            fd.cols_field = f"{fd.name}_cols"
            result.append(fd)
            result.append(FieldDef(name=fd.rows_field, fbs_type="[int32]", role="dim", size_dim=fd.rows_dim))
            result.append(FieldDef(name=fd.cols_field, fbs_type="[int32]", role="dim", size_dim=fd.cols_dim))
        else:
            result.append(fd)
    return result


# =============================================================================
# FBS Generation
# =============================================================================


def _table_prefix(data: dict) -> str:
    """Get the FBS table prefix, stripping 'Skill' suffix by default."""
    skill = data["skill"]
    prefix = skill.get("table_prefix")
    if prefix:
        return prefix
    name = skill["class_name"]
    return name[:-5] if name.endswith("Skill") else name


def generate_fbs(data: dict) -> str:
    """Generate FlatBuffers schema text from skill definition."""
    skill = data["skill"]
    class_name = skill["class_name"]
    prefix = _table_prefix(data)
    description = skill["description"]

    req_fields = parse_fields(data["request"]["fields"])
    resp_fields = parse_fields(data["response"]["fields"])

    lines = [
        f"// {class_name}: {description}",
        f"// Auto-generated from {class_name}.skill.toml — DO NOT EDIT",
        "",
        f"namespace {FBS_NAMESPACE};",
        "",
    ]

    lines.append(f"table {prefix}Request {{")

    for f in req_fields:
        comment = ""
        if f.role == "dim":
            comment = "  // Scalar-as-vector"
        elif f.role == "scalar":
            comment = "  // Scalar-as-vector"
        elif f.role == "matrix":
            comment = f"  // Matrix data, length = {f.rows_field} * {f.cols_field}"
        lines.append(f"  {f.name}: {f.fbs_type};{comment}")

    lines.append("}")
    lines.append("")
    lines.append(f"table {prefix}Response {{")

    for f in resp_fields:
        comment = ""
        if f.role == "dim":
            comment = "  // Scalar-as-vector"
        elif f.role == "matrix":
            comment = f"  // Matrix data, length = {f.rows_field} * {f.cols_field}"
        lines.append(f"  {f.name}: {f.fbs_type};{comment}")

    lines.append("}")
    lines.append("")

    return "\n".join(lines)


# =============================================================================
# HPP Generation — Helper Functions
# =============================================================================

def _emit_ptrs_struct(struct_name: str, fields: list[FieldDef]) -> str:
    """Generate a Ptrs struct declaration."""
    lines = [f"struct {struct_name} {{"]
    for f in fields:
        handler = get_role_handler(f.role)
        member = handler.ptrs_member(f)
        if member is not None:
            lines.append(member)
    lines.append("};")
    return "\n".join(lines)


def _build_shape_info(data: dict) -> dict:
    """
    Extract shape dimension info from the skill definition.

    Returns dict with:
      - dims: list of dimension names (e.g., ['m', 'k', 'n'])
      - field_map: dict mapping FBS field name → shape dim name (from size_dim)
      - dim_type: C++ type for shape members (int32_t for matrices, size_t for vectors/bytes)
      - req_fields: parsed request FieldDef list (for ptrs access resolution)
    """
    shape = data.get("shape", {})
    dims = shape.get("dims", [])

    req_fields = parse_fields(data["request"]["fields"])

    # Build field_map from size_dim on all request fields (dim, vector, bytes)
    field_map = {f.name: f.size_dim for f in req_fields if f.size_dim}

    # Determine dim type: size_t if any vector/bytes dims, int32_t for matrices
    dim_type = "int32_t"
    for f in req_fields:
        if f.role in ("vector", "bytes") and f.size_dim:
            dim_type = "size_t"
            break

    return {
        "dims": dims,
        "field_map": field_map,
        "dim_type": dim_type,
        "req_fields": req_fields,
    }


def _emit_shape_struct(shape_info: dict) -> str:
    """Generate the RequestShape struct."""
    dim_type = shape_info["dim_type"]
    lines = ["struct RequestShape {"]
    for dim_name in shape_info["dims"]:
        lines.append(f"    {dim_type} {dim_name};")
    lines.append("};")
    return "\n".join(lines)


def _shape_field_map(shape_info: dict) -> dict[str, str]:
    """Get field map: FBS dim field name → shape dimension name."""
    return shape_info["field_map"]


def _response_shape_map(data: dict) -> dict[str, str]:
    """Build map from response field name → shape dimension name (from size_dim)."""
    resp_fields = parse_fields(data["response"]["fields"])
    return {f.name: f.size_dim for f in resp_fields if f.size_dim}


# =============================================================================
# HPP Generation — Scatter Methods
# =============================================================================

def _emit_scatter_request(class_name: str, table_prefix: str, fields: list[FieldDef], shape_info: dict) -> str:
    """Generate scatter_request method."""
    lines = [
        "[[nodiscard]] static std::optional<RequestPtrs> scatter_request(",
        "    std::span<const uint8_t> payload",
        ") {",
        f"    auto* request = flatbuffers::GetMutableRoot<{table_prefix}Request>(",
        "        const_cast<uint8_t*>(payload.data()));",
        "    if (!request) return std::nullopt;",
        "",
    ]

    # Null checks — skip dim fields (handled by their parent matrix)
    null_checks = []
    for f in fields:
        if f.role == "dim":
            continue  # matrix handler already checks dim fields
        handler = get_role_handler(f.role)
        checks = handler.scatter_null_check(f, "request")
        if isinstance(checks, list):
            null_checks.extend(checks)
        else:
            null_checks.append(checks)

    if null_checks:
        conditions = "\n        || !".join(null_checks)
        lines.append(f"    if (!{conditions}) {{")
        lines.append("        return std::nullopt;")
        lines.append("    }")
        lines.append("")

    # Mutable vector extraction
    for f in fields:
        handler = get_role_handler(f.role)
        if f.role == "dim":
            continue  # dims are extracted via their parent matrix
        if hasattr(handler, "scatter_code"):
            lines.extend(handler.scatter_code(f, "request"))

    lines.append("")

    # Auto-derive size consistency for vector/bytes fields sharing same size_dim
    from collections import defaultdict
    size_dim_groups = defaultdict(list)
    for f in fields:
        if f.role in ("vector", "bytes") and f.size_dim:
            size_dim_groups[f.size_dim].append(f)
    for dim, group in size_dim_groups.items():
        if len(group) >= 2:
            first = group[0]
            for other in group[1:]:
                lines.append(f"    if (vec_{first.name}->size() != vec_{other.name}->size()) return std::nullopt;")
            lines.append("")

    # Scalar-as-vector size validation
    scalar_checks = []
    for f in fields:
        if f.role == "scalar":
            handler = get_role_handler(f.role)
            scalar_checks.append(handler.scatter_validation(f))
        elif f.role == "matrix":
            handler = get_role_handler(f.role)
            scalar_checks.extend(handler.scatter_dim_validation(f))

    if scalar_checks:
        conditions = "\n        || ".join(scalar_checks)
        lines.append(f"    if ({conditions}) {{")
        lines.append("        return std::nullopt;")
        lines.append("    }")
        lines.append("")

    # Extract dimension values for matrices
    for f in fields:
        if f.role == "matrix":
            handler = get_role_handler(f.role)
            lines.extend(handler.scatter_dim_extract(f))

    # Build MatrixSpan for matrix fields
    matrix_fields = [f for f in fields if f.role == "matrix"]
    if matrix_fields:
        lines.append("")
        for f in matrix_fields:
            handler = get_role_handler(f.role)
            lines.extend(handler.scatter_matrix_span(f))

        # Validity checks
        valid_checks = [get_role_handler(f.role).scatter_valid_check(f) for f in matrix_fields]
        conditions = " || !".join(valid_checks)
        lines.append(f"    if (!{conditions}) return std::nullopt;")

    # Build return struct
    lines.append("")
    lines.append("    return RequestPtrs{")
    ptrs_inits = []
    for f in fields:
        if f.role == "dim":
            continue
        handler = get_role_handler(f.role)
        init = handler.scatter_ptrs_init(f)
        if isinstance(init, list):
            ptrs_inits.extend(init)
        else:
            ptrs_inits.append(init)
    lines.append(",\n".join(ptrs_inits))
    lines.append("    };")
    lines.append("}")

    return "\n".join(lines)


def _emit_scatter_response(class_name: str, table_prefix: str, fields: list[FieldDef]) -> str:
    """Generate scatter_response method."""
    lines = [
        "[[nodiscard]] static std::optional<ResponsePtrs> scatter_response(",
        "    std::span<uint8_t> payload",
        ") {",
        f"    auto* response = flatbuffers::GetMutableRoot<{table_prefix}Response>(payload.data());",
    ]

    # Null checks — skip dim fields (handled by their parent matrix)
    null_checks = []
    for f in fields:
        if f.role == "dim":
            continue
        handler = get_role_handler(f.role)
        checks = handler.scatter_null_check(f, "response")
        if isinstance(checks, list):
            null_checks.extend(checks)
        else:
            null_checks.append(checks)

    conditions = " || !".join(null_checks)
    lines.append(f"    if (!response || !{conditions}) {{")
    lines.append("        return std::nullopt;")
    lines.append("    }")
    lines.append("")

    # Mutable vector extraction
    for f in fields:
        handler = get_role_handler(f.role)
        if f.role == "dim":
            continue
        if hasattr(handler, "scatter_code"):
            lines.extend(handler.scatter_code(f, "response"))
    lines.append("")

    # Scalar-as-vector validation (response scalars and matrix dims)
    scalar_checks = []
    for f in fields:
        if f.role == "scalar":
            handler = get_role_handler(f.role)
            scalar_checks.append(handler.scatter_validation(f))
        elif f.role == "matrix":
            handler = get_role_handler(f.role)
            scalar_checks.extend(handler.scatter_dim_validation(f))

    if scalar_checks:
        conditions = " || ".join(scalar_checks)
        lines.append(f"    if ({conditions}) {{")
        lines.append("        return std::nullopt;")
        lines.append("    }")
        lines.append("")

    # Build MatrixSpan for matrix fields
    matrix_fields = [f for f in fields if f.role == "matrix"]
    if matrix_fields:
        for f in matrix_fields:
            handler = get_role_handler(f.role)
            lines.extend(handler.scatter_dim_extract(f))
            lines.extend(handler.scatter_matrix_span(f))

        valid_checks = [get_role_handler(f.role).scatter_valid_check(f) for f in matrix_fields]
        conditions = " || !".join(valid_checks)
        lines.append(f"    if (!{conditions}) return std::nullopt;")
        lines.append("")

    # Build return struct
    lines.append("    return ResponsePtrs{")
    ptrs_inits = []
    for f in fields:
        if f.role == "dim":
            continue
        handler = get_role_handler(f.role)
        init = handler.scatter_ptrs_init(f)
        if isinstance(init, list):
            ptrs_inits.extend(init)
        else:
            ptrs_inits.append(init)
    lines.append(",\n".join(ptrs_inits))
    lines.append("    };")
    lines.append("}")

    return "\n".join(lines)


# =============================================================================
# HPP Generation — Factory Methods
# =============================================================================

def _emit_create_request(class_name: str, table_prefix: str, req_fields: list[FieldDef], shape_info: dict) -> str:
    """Generate create_request(const RequestShape&) factory."""
    dim_map = _shape_field_map(shape_info)  # fbs_field -> shape.dim_name

    maybe_unused = "[[maybe_unused]] " if not shape_info["dims"] else ""
    lines = [
        f"[[nodiscard]] static {class_name}Payload create_request(",
        f"    {maybe_unused}const RequestShape& shape",
        ") {",
    ]

    # Estimate buffer size
    estimate_parts = ["128"]
    for f in req_fields:
        if f.role == "matrix":
            rows_expr = f"shape.{dim_map[f.rows_field]}"
            cols_expr = f"shape.{dim_map[f.cols_field]}"
            estimate_parts.append(f"static_cast<size_t>({rows_expr}) * {cols_expr} * sizeof({f.cpp_type})")
        elif f.role == "vector" and f.size_dim:
            estimate_parts.append(f"shape.{f.size_dim} * sizeof({f.cpp_type})")
        elif f.role == "bytes" and f.size_dim:
            estimate_parts.append(f"shape.{f.size_dim} * sizeof({f.cpp_type})")
        elif f.role == "scalar":
            estimate_parts.append(f"sizeof({f.cpp_type})")
        elif f.role == "dim":
            estimate_parts.append("sizeof(int32_t)")

    lines.append(f"    size_t est = {' + '.join(estimate_parts)};")
    lines.append("    flatbuffers::FlatBufferBuilder builder(est);")
    lines.append("")

    # Declare pointers and create vectors
    for f in req_fields:
        handler = get_role_handler(f.role)
        if f.role == "matrix":
            rows_expr = f"shape.{dim_map[f.rows_field]}"
            cols_expr = f"shape.{dim_map[f.cols_field]}"
            lines.extend(handler.factory_decl(f, rows_expr, cols_expr))
        elif f.role == "scalar":
            lines.extend(handler.factory_decl(f))
        elif f.role == "vector":
            size_expr = f"shape.{f.size_dim}" if f.size_dim else "0"
            lines.extend(handler.factory_decl(f, size_expr))
        elif f.role == "dim":
            pass  # Handled by matrix
        elif f.role == "bytes":
            size_expr = f"shape.{f.size_dim}" if f.size_dim else "0"
            lines.extend(handler.factory_decl(f, size_expr))

    lines.append("")

    # Set dimension scalars
    for f in req_fields:
        if f.role == "matrix":
            handler = get_role_handler(f.role)
            rows_expr = f"shape.{dim_map[f.rows_field]}"
            cols_expr = f"shape.{dim_map[f.cols_field]}"
            lines.extend(handler.factory_dim_assign(f, rows_expr, cols_expr))
        elif f.role == "scalar":
            # Set default values for scalars
            if f.cpp_type == "double":
                default = "0.0"
            else:
                default = "0"
            lines.append(f"    *ptr_{f.name} = {default};")

    lines.append("")

    # Build FlatBuffer
    fields_args = ", ".join(f"off_{f.name}" for f in req_fields)
    lines.append(f"    auto request = Create{table_prefix}Request(builder,")
    lines.append(f"        {fields_args});")
    lines.append("    builder.Finish(request);")
    lines.append("")
    lines.append("    auto detached = builder.Release();")
    lines.append("")

    # Re-parse for stable pointers
    lines.append(f"    auto* req = flatbuffers::GetMutableRoot<{table_prefix}Request>(detached.data());")
    lines.append("")

    # Build Ptrs struct from reparsed buffer
    lines.append("    RequestPtrs ptrs{")
    ptrs_inits = []
    for f in req_fields:
        if f.role == "dim":
            continue
        handler = get_role_handler(f.role)
        if f.role == "matrix":
            rows_expr = f"shape.{dim_map[f.rows_field]}"
            cols_expr = f"shape.{dim_map[f.cols_field]}"
            result = handler.factory_reparse_ptrs(f, "req", rows_expr, cols_expr)
        elif f.role == "scalar":
            result = handler.factory_reparse_ptrs(f, "req")
        elif f.role == "vector":
            size_expr = f"shape.{f.size_dim}" if f.size_dim else "0"
            result = handler.factory_reparse_ptrs(f, "req", size_expr)
        elif f.role == "bytes":
            size_expr = f"shape.{f.size_dim}" if f.size_dim else "0"
            result = handler.factory_reparse_ptrs(f, "req", size_expr)
        else:
            continue
        if isinstance(result, list):
            ptrs_inits.extend(result)
        else:
            ptrs_inits.append(result)

    lines.append(",\n".join(ptrs_inits))
    lines.append("    };")
    lines.append("")
    lines.append(f"    return {class_name}Payload(std::move(detached), ptrs, kSkillId());")
    lines.append("}")

    return "\n".join(lines)


def _emit_create_response(class_name: str, table_prefix: str, resp_fields: list[FieldDef],
                           shape_info: dict, resp_shape_map: dict) -> str:
    """Generate create_response(...) factory taking shape dimension args."""
    dim_map = _shape_field_map(shape_info)

    # Determine response factory parameters from shape dims used
    # e.g., for Dgemm: c_rows → m, c_cols → n → params are int32_t m, int32_t n
    used_dims = set()
    for resp_dim_field, shape_dim in resp_shape_map.items():
        used_dims.add(shape_dim)

    dim_type = shape_info["dim_type"]

    # Parameter list: the shape dims used by response
    params = []
    for dim_name in shape_info["dims"]:
        if dim_name in used_dims:
            params.append(f"{dim_type} {dim_name}")

    param_str = ", ".join(params)

    lines = [
        f"[[nodiscard]] static {class_name}ResponseBuffer create_response({param_str}) {{",
    ]

    # Estimate size
    estimate_parts = ["64"]
    for f in resp_fields:
        if f.role == "matrix":
            rows_dim = resp_shape_map.get(f.rows_field, f.rows_field)
            cols_dim = resp_shape_map.get(f.cols_field, f.cols_field)
            estimate_parts.append(f"static_cast<size_t>({rows_dim}) * {cols_dim} * sizeof({f.cpp_type})")
        elif f.role == "dim":
            estimate_parts.append("sizeof(int32_t)")
        elif f.role == "vector":
            dim = resp_shape_map.get(f.name)
            if dim:
                estimate_parts.append(f"{dim} * sizeof({f.cpp_type})")
        elif f.role == "bytes":
            dim = resp_shape_map.get(f.name)
            if dim:
                estimate_parts.append(f"{dim} * sizeof({f.cpp_type})")
        elif f.role == "scalar":
            estimate_parts.append(f"sizeof({f.cpp_type})")
    lines.append(f"    size_t est = {' + '.join(estimate_parts)};")
    lines.append("    flatbuffers::FlatBufferBuilder builder(est);")
    lines.append("")

    # Create vectors
    for f in resp_fields:
        handler = get_role_handler(f.role)
        if f.role == "matrix":
            rows_dim = resp_shape_map.get(f.rows_field, f.rows_field)
            cols_dim = resp_shape_map.get(f.cols_field, f.cols_field)
            lines.extend(handler.factory_decl(f, rows_dim, cols_dim))
        elif f.role == "dim":
            pass  # Handled by matrix
        elif f.role == "scalar":
            lines.extend(handler.factory_decl(f))
        elif f.role == "vector":
            dim = resp_shape_map.get(f.name, "0")
            lines.extend(handler.factory_decl(f, dim))
        elif f.role == "bytes":
            dim = resp_shape_map.get(f.name, "0")
            lines.extend(handler.factory_decl(f, dim))
    lines.append("")

    # Set dimension values
    for f in resp_fields:
        if f.role == "matrix":
            handler = get_role_handler(f.role)
            rows_dim = resp_shape_map.get(f.rows_field, f.rows_field)
            cols_dim = resp_shape_map.get(f.cols_field, f.cols_field)
            lines.extend(handler.factory_dim_assign(f, rows_dim, cols_dim))

    # Zero-initialize data (safe default)
    for f in resp_fields:
        if f.role == "matrix":
            lines.append(f"    for (size_t i = 0; i < {f.name}_size; ++i) ptr_{f.name}[i] = 0.0;")
        elif f.role == "vector":
            dim = resp_shape_map.get(f.name)
            if dim:
                zero = "0.0" if f.cpp_type in ("double", "float") else "0"
                lines.append(f"    for (size_t i = 0; i < {dim}; ++i) ptr_{f.name}[i] = {zero};")
        elif f.role == "bytes":
            dim = resp_shape_map.get(f.name)
            if dim:
                lines.append(f"    for (size_t i = 0; i < {dim}; ++i) ptr_{f.name}[i] = 0;")
        elif f.role == "scalar":
            zero = "0.0" if f.cpp_type in ("double", "float") else "0"
            lines.append(f"    *ptr_{f.name} = {zero};")
    lines.append("")

    # Build FlatBuffer
    fields_args = ", ".join(f"off_{f.name}" for f in resp_fields)
    lines.append(f"    auto response = Create{table_prefix}Response(builder, {fields_args});")
    lines.append("    builder.Finish(response);")
    lines.append("")
    lines.append("    auto detached = builder.Release();")
    lines.append("")
    lines.append(f"    auto* resp = flatbuffers::GetMutableRoot<{table_prefix}Response>(detached.data());")
    lines.append("")

    # Build Ptrs struct
    lines.append("    ResponsePtrs ptrs{")
    ptrs_inits = []
    for f in resp_fields:
        if f.role == "dim":
            continue
        handler = get_role_handler(f.role)
        if f.role == "matrix":
            rows_dim = resp_shape_map.get(f.rows_field, f.rows_field)
            cols_dim = resp_shape_map.get(f.cols_field, f.cols_field)
            result = handler.factory_reparse_ptrs(f, "resp", rows_dim, cols_dim)
        elif f.role == "scalar":
            result = handler.factory_reparse_ptrs(f, "resp")
        elif f.role == "vector":
            dim = resp_shape_map.get(f.name, "0")
            result = handler.factory_reparse_ptrs(f, "resp", dim)
        elif f.role == "bytes":
            dim = resp_shape_map.get(f.name, "0")
            result = handler.factory_reparse_ptrs(f, "resp", dim)
        else:
            continue
        if isinstance(result, list):
            ptrs_inits.extend(result)
        else:
            ptrs_inits.append(result)
    lines.append(",\n".join(ptrs_inits))
    lines.append("    };")
    lines.append("")
    lines.append(f"    return {class_name}ResponseBuffer(std::move(detached), ptrs, kSkillId());")
    lines.append("}")

    return "\n".join(lines)


def _emit_create_response_for_request(class_name: str, resp_shape_map: dict,
                                        shape_info: dict) -> str:
    """Generate create_response_for_request() overloads."""
    # Map from shape dim → how to extract from request ptrs
    dim_map = _shape_field_map(shape_info)

    # Build extraction expressions: for matrix dims, use req_ptrs->matrix.rows / .cols
    # We need to know which request matrix each dim belongs to
    # This info is in field_types - dims reference their parent matrix's rows_field/cols_field

    # Response args are the shape dims used by response
    used_dims = set(resp_shape_map.values())
    # Build reverse map: shape dim → first FBS field that maps to it
    dim_to_first_field = {}
    for fbs_field, shape_dim in dim_map.items():
        if shape_dim not in dim_to_first_field:
            dim_to_first_field[shape_dim] = fbs_field
    arg_exprs = []
    for dim_name in shape_info["dims"]:
        if dim_name in used_dims:
            from_field = dim_to_first_field[dim_name]
            arg_exprs.append((dim_name, from_field))

    lines = [
        "[[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(",
        "    std::span<const uint8_t> request",
        ") {",
        "    auto req_ptrs = scatter_request(request);",
        "    if (!req_ptrs) return nullptr;",
    ]

    # Build the create_response call args by extracting dims from request ptrs
    # For matrices: req_ptrs->a.rows for a_rows, req_ptrs->b.cols for b_cols, etc.
    call_args = []
    for dim_name, from_field in arg_exprs:
        # Find which matrix owns this dim field
        call_args.append(f"req_ptrs->{_dim_to_ptrs_expr(from_field, shape_info)}")

    args_str = ", ".join(call_args)
    lines.append(f"    return std::make_unique<{class_name}ResponseBuffer>(")
    lines.append(f"        create_response({args_str}));")
    lines.append("}")
    lines.append("")

    # Overload taking PayloadBufferBase
    lines.append("[[nodiscard]] static std::unique_ptr<PayloadBufferBase> create_response_for_request(")
    lines.append("    const PayloadBufferBase& request")
    lines.append(") {")
    lines.append("    return create_response_for_request(request.span());")
    lines.append("}")

    return "\n".join(lines)


def _dim_to_ptrs_expr(dim_field: str, shape_info: dict) -> str:
    """
    Convert a FBS dimension field name to a RequestPtrs access expression.

    e.g., 'a_rows' → 'a.rows', 'b_cols' → 'b.cols'
    For vectors: ptr_name.size(), for bytes: ptr_name_length.
    """
    # Check if this is a vector/bytes request field
    for f in shape_info.get("req_fields", []):
        if f.name == dim_field:
            if f.role == "vector":
                return f"{f.ptr_name}.size()"
            elif f.role == "bytes":
                return f"{f.ptr_name}_length"

    # Matrix convention: <matrix_ptr_name>_rows / <matrix_ptr_name>_cols
    if dim_field.endswith("_rows"):
        matrix_name = dim_field[:-5]  # strip '_rows'
        return f"{matrix_name}.rows"
    elif dim_field.endswith("_cols"):
        matrix_name = dim_field[:-5]  # strip '_cols'
        return f"{matrix_name}.cols"
    else:
        return f"{dim_field}"


# =============================================================================
# HPP Generation — compare
# =============================================================================

def _emit_compare_response(resp_fields: list[FieldDef]) -> str:
    """Generate compare_response() method."""
    lines = [
        "[[nodiscard]] static VerificationResult compare_response(",
        "    const ResponsePtrs& computed,",
        "    const ResponsePtrs& worker",
        ") {",
    ]

    compare_calls = []
    for f in resp_fields:
        if f.role == "dim":
            continue
        handler = get_role_handler(f.role)
        if hasattr(handler, "compare_code"):
            compare_calls.append(handler.compare_code(f))

    if len(compare_calls) == 1:
        lines.append(f"    return {compare_calls[0].strip()};")
    else:
        for i, call in enumerate(compare_calls):
            if i == 0:
                lines.append(f"    auto result = {call.strip()};")
                lines.append("    if (!result.passed) return result;")
            elif i == len(compare_calls) - 1:
                lines.append(f"    return {call.strip()};")
            else:
                lines.append(f"    result = {call.strip()};")
                lines.append("    if (!result.passed) return result;")

    lines.append("}")
    return "\n".join(lines)


# =============================================================================
# HPP Generation — developer reference comment
# =============================================================================

def _emit_developer_comment(req_fields: list[FieldDef], resp_fields: list[FieldDef],
                             shape_info: dict) -> str:
    """Generate a structured comment showing Ptrs layouts and shape dims."""
    lines = ["// =========================================================================",
             "// Developer-implemented (defined in _impl.hpp)",
             "//"]

    def _field_desc(f: FieldDef) -> str:
        if f.role == "matrix":
            return f"//   MatrixSpan {f.ptr_name}   — {f.rows_dim} × {f.cols_dim}"
        elif f.role == "vector":
            dim = f" — size: {f.size_dim}" if f.size_dim else ""
            return f"//   std::span<{f.cpp_type}> {f.ptr_name}{dim}"
        elif f.role == "bytes":
            dim = f" — size: {f.size_dim}" if f.size_dim else ""
            return f"//   {f.cpp_type}* {f.ptr_name} + size_t {f.ptr_name}_length{dim}"
        elif f.role == "scalar":
            return f"//   {f.cpp_type}* {f.ptr_name}"
        return None

    lines.append("// RequestPtrs:")
    for f in req_fields:
        desc = _field_desc(f)
        if desc:
            lines.append(desc)

    lines.append("//")
    lines.append("// ResponsePtrs:")
    for f in resp_fields:
        desc = _field_desc(f)
        if desc:
            lines.append(desc)

    if shape_info["dims"]:
        lines.append("//")
        dims_str = ", ".join(shape_info["dims"])
        lines.append(f"// Shape dims: {dims_str}")

    lines.append("// =========================================================================")
    return "\n".join(lines)


# =============================================================================
# HPP Generation — extract_shape
# =============================================================================

def _emit_extract_shape(shape_info: dict) -> str:
    """Generate extract_shape(req) → RequestShape from request ptrs."""
    dims = shape_info["dims"]
    dim_map = shape_info["field_map"]

    # Reverse map: shape dim → first FBS field that maps to it
    dim_to_first_field = {}
    for fbs_field, shape_dim in dim_map.items():
        if shape_dim not in dim_to_first_field:
            dim_to_first_field[shape_dim] = fbs_field

    if not dims:
        return textwrap.dedent("""\
            static RequestShape extract_shape([[maybe_unused]] const RequestPtrs& req) {
                return RequestShape{};
            }""")

    inits = []
    for dim_name in dims:
        from_field = dim_to_first_field[dim_name]
        expr = _dim_to_ptrs_expr(from_field, shape_info)
        inits.append(f"        .{dim_name} = req.{expr}")

    init_str = ",\n".join(inits)
    return (
        "static RequestShape extract_shape(const RequestPtrs& req) {\n"
        "    return RequestShape{\n"
        f"{init_str}\n"
        "    };\n"
        "}"
    )


# =============================================================================
# HPP Generation — create_test_request
# =============================================================================

def _emit_create_test_request(class_name: str) -> str:
    """Generate create_test_request() inline — bridges fill_test_request callback to create_request."""
    return textwrap.dedent(f"""\
        [[nodiscard]] static PayloadType create_test_request(size_t case_index = 0) {{
            PayloadType payload;
            fill_test_request(case_index, [&](const RequestShape& shape) -> RequestPtrs& {{
                payload = create_request(shape);
                return payload.ptrs();
            }});
            return payload;
        }}""")


# =============================================================================
# HPP Generation — Main File Assembly
# =============================================================================

def generate_hpp(data: dict, srcdir: str = ".") -> str:
    """Generate the complete _gen.hpp file."""
    skill = data["skill"]
    class_name = skill["class_name"]
    description = skill["description"]
    version = skill["version"]
    skill_name = skill["name"]

    req_fields = parse_fields(data["request"]["fields"])
    resp_fields = parse_fields(data["response"]["fields"])
    shape_info = _build_shape_info(data)
    resp_shape_map = _response_shape_map(data)
    prefix = _table_prefix(data)

    includes = skill.get("includes", [])

    # Detect whether we need MatrixSpan
    has_matrix = any(f.get("role") == "matrix" for f in data["request"]["fields"] + data["response"]["fields"])

    lines = []

    # File header
    lines.extend([
        f"/**",
        f" * @file {class_name}_gen.hpp",
        f" * @brief {description}",
        f" *",
        f" * AUTO-GENERATED from {class_name}.skill.toml — DO NOT EDIT",
        f" */",
        "#pragma once",
        "",
    ])

    # Standard includes
    lines.extend([
        '#include "skills/registry/CompareUtils.hpp"',
        '#include "skills/registry/Skill.hpp"',
        '#include "skills/registry/PayloadBuffer.hpp"',
    ])

    # MatrixSpan for skills with matrix fields
    if has_matrix:
        lines.append('#include "skills/registry/MatrixSpan.hpp"')

    # Extra includes from skill definition
    for inc in includes:
        lines.append(f'#include "{inc}"')

    # Generated FlatBuffers header
    lines.append(f'#include "{class_name}_generated.h"')
    lines.append("")

    # Standard library includes
    lines.extend([
        "#include <functional>",
        "#include <memory>",
        "#include <optional>",
        "#include <span>",
        "",
    ])

    # Open namespace
    lines.append(f"namespace {CPP_NAMESPACE} {{")
    lines.append("")

    # Forward declaration
    lines.append(f"class {class_name};")
    lines.append("")

    # Ptrs structs
    req_prefix = class_name.replace("Skill", "")
    lines.append(_emit_ptrs_struct(f"{class_name}RequestPtrs", req_fields))
    lines.append("")
    lines.append(_emit_ptrs_struct(f"{class_name}ResponsePtrs", resp_fields))
    lines.append("")

    # Type aliases
    lines.append(f"using {class_name}Payload = PayloadBuffer<{class_name}RequestPtrs>;")
    lines.append(f"using {class_name}ResponseBuffer = PayloadBuffer<{class_name}ResponsePtrs>;")
    lines.append("")

    # Class opening
    lines.append(f"class {class_name} : public Skill<{class_name}> {{")
    lines.append("public:")

    # Type aliases
    lines.extend([
        "    using RequestPtrs = " + f"{class_name}RequestPtrs;",
        "    using ResponsePtrs = " + f"{class_name}ResponsePtrs;",
        f'    using PayloadType = {class_name}Payload;',
        "",
        f'    static constexpr std::string_view kSkillName = "{skill_name}";',
        f'    static constexpr std::string_view kSkillDescription = "{description}";',
        f"    static constexpr uint32_t kSkillVersion = {version};",
        "",
    ])

    # RequestShape struct
    lines.append("    " + _emit_shape_struct(shape_info).replace("\n", "\n    "))
    lines.append("")

    # Scatter methods
    lines.append("    // =========================================================================")
    lines.append("    // Scatter methods")
    lines.append("    // =========================================================================")
    lines.append("")
    lines.append("    " + _emit_scatter_request(class_name, prefix, req_fields, shape_info).replace("\n", "\n    "))
    lines.append("")
    lines.append("    " + _emit_scatter_response(class_name, prefix, resp_fields).replace("\n", "\n    "))
    lines.append("")

    # Developer-implemented method declarations (with reference comment)
    lines.append("    " + _emit_developer_comment(req_fields, resp_fields, shape_info).replace("\n", "\n    "))
    lines.append("")
    lines.append("    bool compute(const RequestPtrs& req, ResponsePtrs& resp);")
    lines.append("    static void fill_test_request(size_t case_index,")
    lines.append("        std::function<RequestPtrs&(const RequestShape&)> allocate_request);")
    lines.append("    static size_t get_test_case_count() noexcept;")
    lines.append("")

    # extract_shape helper
    lines.append("    " + _emit_extract_shape(shape_info).replace("\n", "\n    "))
    lines.append("")

    # Test support — generated inline, bridges callback to create_request
    lines.append("    // =========================================================================")
    lines.append("    // Test cases")
    lines.append("    // =========================================================================")
    lines.append("")
    lines.append("    " + _emit_create_test_request(class_name).replace("\n", "\n    "))
    lines.append("")

    # Response factory
    lines.append("    // =========================================================================")
    lines.append("    // Response factory")
    lines.append("    // =========================================================================")
    lines.append("")
    lines.append("    " + _emit_create_response_for_request(class_name, resp_shape_map, shape_info).replace("\n", "\n    "))
    lines.append("")

    # Factory methods
    lines.append("    // =========================================================================")
    lines.append("    // Factory methods")
    lines.append("    // =========================================================================")
    lines.append("")
    lines.append("    " + _emit_create_request(class_name, prefix, req_fields, shape_info).replace("\n", "\n    "))
    lines.append("")
    lines.append("    " + _emit_create_response(class_name, prefix, resp_fields, shape_info, resp_shape_map).replace("\n", "\n    "))
    lines.append("")

    # Verification
    lines.append("    // =========================================================================")
    lines.append("    // Verification Support")
    lines.append("    // =========================================================================")
    lines.append("")
    lines.append("    " + _emit_compare_response(resp_fields).replace("\n", "\n    "))

    # Close class and namespace
    lines.append("};")
    lines.append("")
    lines.append(f"}} // namespace {CPP_NAMESPACE}")
    lines.append("")

    return "\n".join(lines)


# =============================================================================
# CPP Registration File Generation
# =============================================================================

def generate_cpp(data: dict) -> str:
    """Generate the registration .cpp file."""
    skill = data["skill"]
    class_name = skill["class_name"]
    description = skill["description"]

    return textwrap.dedent(f"""\
        /**
         * @file {class_name}.cpp
         * @brief Registration for {class_name}.
         *
         * {description}
         * AUTO-GENERATED from {class_name}.skill.toml — DO NOT EDIT
         */

        #include "skills/registry/SkillRegistration.hpp"
        #include "{class_name}_impl.hpp"

        namespace {CPP_NAMESPACE} {{

        REGISTER_SKILL_CLASS({class_name});

        }} // namespace {CPP_NAMESPACE}
    """)


# =============================================================================
# CLI
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Generate skill boilerplate from .skill.toml definitions."
    )
    parser.add_argument("input", help="Path to .skill.toml file")
    parser.add_argument("--outdir", required=True, help="Output directory for generated files")
    parser.add_argument("--srcdir", default=".", help="Source directory (for relative include paths)")

    args = parser.parse_args()

    data = load_skill_def(args.input)
    class_name = data["skill"]["class_name"]

    os.makedirs(args.outdir, exist_ok=True)

    # Generate .fbs
    fbs_path = os.path.join(args.outdir, f"{class_name}.fbs")
    with open(fbs_path, "w", encoding="utf-8") as f:
        f.write(generate_fbs(data))
    print(f"Generated: {fbs_path}")

    # Generate _gen.hpp
    hpp_path = os.path.join(args.outdir, f"{class_name}_gen.hpp")
    with open(hpp_path, "w", encoding="utf-8") as f:
        f.write(generate_hpp(data, args.srcdir))
    print(f"Generated: {hpp_path}")

    # Generate .cpp
    cpp_path = os.path.join(args.outdir, f"{class_name}.cpp")
    with open(cpp_path, "w", encoding="utf-8") as f:
        f.write(generate_cpp(data))
    print(f"Generated: {cpp_path}")


if __name__ == "__main__":
    main()
