"""
Field role types for the skill code generator.

Each role class knows how to emit the C++ code fragments for its field type:
- Ptrs struct member declaration
- Scatter code (GetMutableRoot → pointer extraction)
- Factory code (CreateUninitializedVector → builder calls)
- Validation code (null checks, size checks)
- Compare code (compare_vector / compare_bytes / compare_int)
"""

from dataclasses import dataclass, field as dc_field
from typing import Optional


def _fbs_inner_type(fbs_type: str) -> str:
    """Extract inner type from FBS vector notation: '[double]' -> 'double'."""
    return fbs_type.strip("[]")


def _cpp_type(fbs_inner: str) -> str:
    """Map FBS scalar type names to C++ types."""
    mapping = {
        "double": "double",
        "float": "float",
        "int8": "int8_t",
        "uint8": "uint8_t",
        "int16": "int16_t",
        "uint16": "uint16_t",
        "int32": "int32_t",
        "uint32": "uint32_t",
        "int64": "int64_t",
        "uint64": "uint64_t",
        "bool": "bool",
    }
    return mapping.get(fbs_inner, fbs_inner)


@dataclass
class FieldDef:
    """Raw field definition parsed from skill definition."""
    name: str
    fbs_type: str
    role: str
    ptr_name: Optional[str] = None
    rows_field: Optional[str] = None
    cols_field: Optional[str] = None
    size_dim: Optional[str] = None
    rows_dim: Optional[str] = None
    cols_dim: Optional[str] = None

    @property
    def fbs_inner(self) -> str:
        return _fbs_inner_type(self.fbs_type)

    @property
    def cpp_type(self) -> str:
        return _cpp_type(self.fbs_inner)


# =============================================================================
# Role handlers - each knows how to emit code for its role type
# =============================================================================


class VectorRole:
    """Role: vector → std::span<T> in Ptrs, sized vector in FBS."""

    @staticmethod
    def ptrs_member(f: FieldDef) -> str:
        return f"    std::span<{f.cpp_type}> {f.ptr_name};"

    @staticmethod
    def scatter_code(f: FieldDef, msg_var: str) -> list[str]:
        """Emit scatter: extract mutable vector, build span."""
        lines = [
            f"    auto* vec_{f.name} = {msg_var}->mutable_{f.name}();",
        ]
        return lines

    @staticmethod
    def scatter_ptrs_init(f: FieldDef) -> str:
        return f"        .{f.ptr_name} = std::span<{f.cpp_type}>(vec_{f.name}->data(), vec_{f.name}->size())"

    @staticmethod
    def scatter_null_check(f: FieldDef, msg_var: str) -> str:
        return f"{msg_var}->{f.name}()"

    @staticmethod
    def factory_decl(f: FieldDef, size_expr: str) -> list[str]:
        return [
            f"    {f.cpp_type}* ptr_{f.name} = nullptr;",
            f"    auto off_{f.name} = builder.CreateUninitializedVector({size_expr}, &ptr_{f.name});",
        ]

    @staticmethod
    def factory_reparse_ptrs(f: FieldDef, root_var: str, size_expr: str) -> str:
        return (
            f"        .{f.ptr_name} = std::span<{f.cpp_type}>("
            f"const_cast<{f.cpp_type}*>({root_var}->{f.name}()->data()), {size_expr})"
        )

    @staticmethod
    def compare_code(f: FieldDef) -> str:
        return f'    compare_vector(computed.{f.ptr_name}, worker.{f.ptr_name}, "{f.ptr_name}")'


class ScalarRole:
    """Role: scalar → T* in Ptrs, single-element vector in FBS."""

    @staticmethod
    def ptrs_member(f: FieldDef) -> str:
        return f"    {f.cpp_type}* {f.ptr_name};"

    @staticmethod
    def scatter_code(f: FieldDef, msg_var: str) -> list[str]:
        return [
            f"    auto* vec_{f.name} = {msg_var}->mutable_{f.name}();",
        ]

    @staticmethod
    def scatter_validation(f: FieldDef) -> str:
        """Scalar-as-vector must have size == 1."""
        return f"vec_{f.name}->size() != 1"

    @staticmethod
    def scatter_ptrs_init(f: FieldDef) -> str:
        return f"        .{f.ptr_name} = vec_{f.name}->data()"

    @staticmethod
    def scatter_null_check(f: FieldDef, msg_var: str) -> str:
        return f"{msg_var}->{f.name}()"

    @staticmethod
    def factory_decl(f: FieldDef, default_val: str = "0") -> list[str]:
        return [
            f"    {f.cpp_type}* ptr_{f.name} = nullptr;",
            f"    auto off_{f.name} = builder.CreateUninitializedVector<{f.cpp_type}>(1, &ptr_{f.name});",
        ]

    @staticmethod
    def factory_reparse_ptrs(f: FieldDef, root_var: str) -> str:
        return f"        .{f.ptr_name} = const_cast<{f.cpp_type}*>({root_var}->{f.name}()->data())"

    @staticmethod
    def compare_code(f: FieldDef) -> str:
        if f.fbs_inner in ("double", "float"):
            return f'    compare_scalar(*computed.{f.ptr_name}, *worker.{f.ptr_name}, "{f.ptr_name}")'
        else:
            return f'    compare_int(*computed.{f.ptr_name}, *worker.{f.ptr_name}, "{f.ptr_name}")'


class MatrixRole:
    """Role: matrix → MatrixSpan in Ptrs, flat vector + dim fields in FBS."""

    @staticmethod
    def ptrs_member(f: FieldDef) -> str:
        return f"    MatrixSpan<{f.cpp_type}> {f.ptr_name};"

    @staticmethod
    def scatter_code(f: FieldDef, msg_var: str) -> list[str]:
        lines = [
            f"    auto* vec_{f.name} = {msg_var}->mutable_{f.name}();",
            f"    auto* vec_{f.rows_field} = {msg_var}->mutable_{f.rows_field}();",
            f"    auto* vec_{f.cols_field} = {msg_var}->mutable_{f.cols_field}();",
        ]
        return lines

    @staticmethod
    def scatter_dim_validation(f: FieldDef) -> list[str]:
        """Dim fields must be single-element vectors."""
        return [
            f"vec_{f.rows_field}->size() != 1",
            f"vec_{f.cols_field}->size() != 1",
        ]

    @staticmethod
    def scatter_dim_extract(f: FieldDef) -> list[str]:
        return [
            f"    int32_t {f.rows_field} = vec_{f.rows_field}->Get(0);",
            f"    int32_t {f.cols_field} = vec_{f.cols_field}->Get(0);",
        ]

    @staticmethod
    def scatter_matrix_span(f: FieldDef) -> list[str]:
        return [
            f"    MatrixSpan<{f.cpp_type}> mat_{f.ptr_name}{{",
            f"        .data = std::span<{f.cpp_type}>(vec_{f.name}->data(), vec_{f.name}->size()),",
            f"        .rows = {f.rows_field},",
            f"        .cols = {f.cols_field}",
            f"    }};",
        ]

    @staticmethod
    def scatter_valid_check(f: FieldDef) -> str:
        return f"mat_{f.ptr_name}.valid()"

    @staticmethod
    def scatter_ptrs_init(f: FieldDef) -> str:
        return f"        .{f.ptr_name} = mat_{f.ptr_name}"

    @staticmethod
    def scatter_null_check(f: FieldDef, msg_var: str) -> list[str]:
        return [
            f"{msg_var}->{f.name}()",
            f"{msg_var}->{f.rows_field}()",
            f"{msg_var}->{f.cols_field}()",
        ]

    @staticmethod
    def factory_decl(f: FieldDef, rows_expr: str, cols_expr: str) -> list[str]:
        size_expr = f"static_cast<size_t>({rows_expr}) * {cols_expr}"
        return [
            f"    size_t {f.name}_size = {size_expr};",
            f"    {f.cpp_type}* ptr_{f.name} = nullptr;",
            f"    int32_t* ptr_{f.rows_field} = nullptr;",
            f"    int32_t* ptr_{f.cols_field} = nullptr;",
            f"    auto off_{f.name} = builder.CreateUninitializedVector({f.name}_size, &ptr_{f.name});",
            f"    auto off_{f.rows_field} = builder.CreateUninitializedVector<int32_t>(1, &ptr_{f.rows_field});",
            f"    auto off_{f.cols_field} = builder.CreateUninitializedVector<int32_t>(1, &ptr_{f.cols_field});",
        ]

    @staticmethod
    def factory_dim_assign(f: FieldDef, rows_expr: str, cols_expr: str) -> list[str]:
        return [
            f"    *ptr_{f.rows_field} = {rows_expr};",
            f"    *ptr_{f.cols_field} = {cols_expr};",
        ]

    @staticmethod
    def factory_reparse_ptrs(f: FieldDef, root_var: str, rows_expr: str, cols_expr: str) -> str:
        size_expr = f"static_cast<size_t>({rows_expr}) * {cols_expr}"
        return (
            f"        .{f.ptr_name} = MatrixSpan<{f.cpp_type}>{{\n"
            f"            .data = std::span<{f.cpp_type}>(\n"
            f"                const_cast<{f.cpp_type}*>({root_var}->{f.name}()->data()), {size_expr}),\n"
            f"            .rows = {rows_expr},\n"
            f"            .cols = {cols_expr}\n"
            f"        }}"
        )

    @staticmethod
    def compare_code(f: FieldDef) -> str:
        return f'    compare_vector(computed.{f.ptr_name}.data, worker.{f.ptr_name}.data, "{f.ptr_name}")'


class DimRole:
    """Role: dim → suppressed from Ptrs (absorbed into parent matrix)."""

    @staticmethod
    def ptrs_member(f: FieldDef) -> None:
        return None  # No Ptrs member

    @staticmethod
    def scatter_null_check(f: FieldDef, msg_var: str) -> str:
        return f"{msg_var}->{f.name}()"


class BytesRole:
    """Role: bytes → T* + size_t length in Ptrs, [int8] vector in FBS."""

    @staticmethod
    def ptrs_member(f: FieldDef) -> str:
        return f"    {f.cpp_type}* {f.ptr_name};\n    size_t {f.ptr_name}_length;"

    @staticmethod
    def scatter_code(f: FieldDef, msg_var: str) -> list[str]:
        return [
            f"    auto* vec_{f.name} = {msg_var}->mutable_{f.name}();",
        ]

    @staticmethod
    def scatter_ptrs_init(f: FieldDef) -> list[str]:
        return [
            f"        .{f.ptr_name} = vec_{f.name}->data()",
            f"        .{f.ptr_name}_length = vec_{f.name}->size()",
        ]

    @staticmethod
    def scatter_null_check(f: FieldDef, msg_var: str) -> str:
        return f"{msg_var}->{f.name}()"

    @staticmethod
    def factory_decl(f: FieldDef, size_expr: str) -> list[str]:
        return [
            f"    {f.cpp_type}* ptr_{f.name} = nullptr;",
            f"    auto off_{f.name} = builder.CreateUninitializedVector({size_expr}, &ptr_{f.name});",
        ]

    @staticmethod
    def factory_reparse_ptrs(f: FieldDef, root_var: str, size_expr: str) -> list[str]:
        return [
            f"        .{f.ptr_name} = const_cast<{f.cpp_type}*>({root_var}->{f.name}()->data())",
            f"        .{f.ptr_name}_length = {size_expr}",
        ]

    @staticmethod
    def compare_code(f: FieldDef) -> str:
        return f'    compare_bytes(std::span<const {f.cpp_type}>(computed.{f.ptr_name}, computed.{f.ptr_name}_length), std::span<const {f.cpp_type}>(worker.{f.ptr_name}, worker.{f.ptr_name}_length), "{f.ptr_name}")'


ROLE_HANDLERS = {
    "vector": VectorRole,
    "scalar": ScalarRole,
    "matrix": MatrixRole,
    "dim": DimRole,
    "bytes": BytesRole,
}


def get_role_handler(role: str):
    """Get the role handler class for a field role name."""
    handler = ROLE_HANDLERS.get(role)
    if handler is None:
        raise ValueError(f"Unknown field role: {role!r}. Valid roles: {list(ROLE_HANDLERS.keys())}")
    return handler
