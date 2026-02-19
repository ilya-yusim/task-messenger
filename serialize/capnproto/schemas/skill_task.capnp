# Skill-based task messaging schema for Cap'n Proto
# This schema defines the serialization format for skill requests and responses.
#
# Cap'n Proto provides:
# - Zero-copy reads (data is accessed directly from the wire format)
# - Schema evolution (add fields without breaking compatibility)
# - Unions for type-safe variants
# - RPC with promise pipelining (optional, see kj-async)

@0xa1b2c3d4e5f60001;  # Unique schema ID

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("TaskMessenger::Skills");

# Enum for math operations
enum MathOperation {
  add @0;
  subtract @1;
  multiply @2;
  divide @3;
}

# String reversal skill request
struct StringReversalRequest {
  input @0 :Text;
}

# String reversal skill response
struct StringReversalResponse {
  output @0 :Text;
  originalLength @1 :UInt32;
}

# Math operation skill request (scalar)
struct MathOperationRequest {
  operandA @0 :Float64;
  operandB @1 :Float64;
  operation @2 :MathOperation;
}

# Math operation skill response
struct MathOperationResponse {
  result @0 :Float64;
  overflow @1 :Bool;
}

# Vector math operation skill (element-wise)
# Demonstrates lists for array data
struct VectorMathRequest {
  operandA @0 :List(Float64);  # First operand vector
  operandB @1 :List(Float64);  # Second operand vector
  operation @2 :MathOperation;
}

struct VectorMathResponse {
  result @0 :List(Float64);  # Result vector
}

# Fused Multiply-Add: result = a + c * b
struct FusedMultiplyAddRequest {
  operandA @0 :List(Float64);   # Vector a
  operandB @1 :List(Float64);   # Vector b
  scalarC @2 :Float64;          # Scalar c
}

struct FusedMultiplyAddResponse {
  result @0 :List(Float64);     # result[i] = a[i] + c * b[i]
}

# Skill request envelope with union for different skill types
# This demonstrates Cap'n Proto unions - type-safe discriminated unions
struct SkillRequest {
  taskId @0 :UInt32;
  
  # Union allows different skill payloads in a type-safe manner
  # Only one variant can be set at a time
  payload :union {
    stringReversal @1 :StringReversalRequest;
    mathOperation @2 :MathOperationRequest;
    vectorMath @3 :VectorMathRequest;
    fusedMultiplyAdd @4 :FusedMultiplyAddRequest;
  }
}

# Skill response envelope
struct SkillResponse {
  taskId @0 :UInt32;
  success @1 :Bool;
  errorMessage @2 :Text;  # Optional error description
  
  payload :union {
    stringReversal @3 :StringReversalResponse;
    mathOperation @4 :MathOperationResponse;
    vectorMath @5 :VectorMathResponse;
    fusedMultiplyAdd @6 :FusedMultiplyAddResponse;
  }
}

# Nested structure example: Task with metadata
struct TaskMetadata {
  createdAt @0 :UInt64;    # Unix timestamp
  priority @1 :UInt8;
  tags @2 :List(Text);     # List of string tags
}

struct TaskWithMetadata {
  metadata @0 :TaskMetadata;
  request @1 :SkillRequest;
}

# Batch request: multiple tasks in one message
struct BatchRequest {
  tasks @0 :List(SkillRequest);
}

struct BatchResponse {
  responses @0 :List(SkillResponse);
}
