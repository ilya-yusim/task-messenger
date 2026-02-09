/**
 * @file test_flatbuffers.cpp
 * @brief Demonstrates FlatBuffers serialization for skill-based task messaging.
 *
 * This test demonstrates the "skill" concept where:
 * - Each skill has a unique ID
 * - Skills have associated request/response data structures
 * - The SkillRequest/SkillResponse envelope wraps skill-specific payloads
 */

#include "skill_task_generated.h"  // Generated from skill_task.fbs

#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>
#include <algorithm>
#include <chrono>

using namespace TaskMessenger::Skills;

namespace {

/**
 * @brief Create a string reversal skill request.
 *
 * This simulates the task generator creating a skill request that will be
 * sent from manager to worker.
 *
 * @param task_id Unique task identifier
 * @param input String to be reversed
 * @return Serialized request bytes ready for transmission
 */
std::vector<uint8_t> create_string_reversal_request(uint32_t task_id, const std::string& input) {
    // First, build the nested StringReversalRequest
    flatbuffers::FlatBufferBuilder inner_builder(256);
    auto input_offset = inner_builder.CreateString(input);
    auto inner_request = CreateStringReversalRequest(inner_builder, input_offset);
    inner_builder.Finish(inner_request);

    // Get the nested payload bytes
    auto nested_bytes = inner_builder.GetBufferPointer();
    auto nested_size = inner_builder.GetSize();

    // Now build the outer SkillRequest envelope
    flatbuffers::FlatBufferBuilder outer_builder(512);
    auto payload = outer_builder.CreateVector(nested_bytes, nested_size);

    auto skill_request = CreateSkillRequest(
        outer_builder,
        1,  // skill_id for string reversal
        task_id,
        payload
    );
    outer_builder.Finish(skill_request);

    // Return as vector for transmission
    return std::vector<uint8_t>(
        outer_builder.GetBufferPointer(),
        outer_builder.GetBufferPointer() + outer_builder.GetSize()
    );
}

/**
 * @brief Process a skill request on the worker side.
 *
 * This simulates the worker receiving a skill request, processing it based
 * on the skill_id, and returning the appropriate response.
 *
 * @param request_bytes Serialized request received from manager
 * @return Serialized response bytes to send back to manager
 */
std::vector<uint8_t> process_skill_request(const std::vector<uint8_t>& request_bytes) {
    // Parse the envelope using the generated GetSkillRequest
    auto skill_request = GetSkillRequest(request_bytes.data());

    uint32_t skill_id = skill_request->skill_id();
    uint32_t task_id = skill_request->task_id();

    std::cout << "Processing skill_id=" << skill_id << ", task_id=" << task_id << "\n";

    if (skill_id == 1) {  // String reversal
        // Parse nested payload using flatbuffers::GetRoot<T>
        auto payload = skill_request->payload();
        auto inner_request = flatbuffers::GetRoot<StringReversalRequest>(payload->data());

        std::string input = inner_request->input()->str();
        std::string output(input.rbegin(), input.rend());

        std::cout << "  Input: \"" << input << "\" -> Output: \"" << output << "\"\n";

        // Build response - first the inner StringReversalResponse
        flatbuffers::FlatBufferBuilder inner_builder(256);
        auto output_offset = inner_builder.CreateString(output);
        auto inner_response = CreateStringReversalResponse(
            inner_builder, output_offset, static_cast<uint32_t>(input.length())
        );
        inner_builder.Finish(inner_response);

        // Wrap in envelope
        flatbuffers::FlatBufferBuilder outer_builder(512);
        auto response_payload = outer_builder.CreateVector(
            inner_builder.GetBufferPointer(), inner_builder.GetSize()
        );
        auto skill_response = CreateSkillResponse(
            outer_builder, skill_id, task_id, true, response_payload
        );
        outer_builder.Finish(skill_response);

        return std::vector<uint8_t>(
            outer_builder.GetBufferPointer(),
            outer_builder.GetBufferPointer() + outer_builder.GetSize()
        );
    }

    // Unknown skill - return error response
    flatbuffers::FlatBufferBuilder builder(128);
    auto response = CreateSkillResponse(builder, skill_id, task_id, false, 0);
    builder.Finish(response);
    return std::vector<uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize()
    );
}

/**
 * @brief Test math operation skill serialization.
 *
 * Demonstrates another skill type with numeric data.
 */
void test_math_operation() {
    std::cout << "\n=== Testing Math Operation Skill ===\n";

    flatbuffers::FlatBufferBuilder builder(256);
    auto math_req = CreateMathOperationRequest(builder, 42.0, 8.0, MathOperation_Multiply);
    builder.Finish(math_req);

    // Simulate round-trip using flatbuffers::GetRoot<T>
    auto buffer = builder.GetBufferPointer();
    auto parsed = flatbuffers::GetRoot<MathOperationRequest>(buffer);

    double a = parsed->operand_a();
    double b = parsed->operand_b();
    MathOperation op = parsed->operation();

    double result = 0;
    switch (op) {
        case MathOperation_Add: result = a + b; break;
        case MathOperation_Subtract: result = a - b; break;
        case MathOperation_Multiply: result = a * b; break;
        case MathOperation_Divide: result = (b != 0) ? a / b : 0; break;
    }

    std::cout << "  " << a << " * " << b << " = " << result << "\n";
    assert(result == 336.0);
    std::cout << "  Math operation test passed!\n";
}

/**
 * @brief Test zero-copy access pattern.
 *
 * FlatBuffers allows accessing data directly from the buffer without copying.
 */
void test_zero_copy_access() {
    std::cout << "\n=== Testing Zero-Copy Access ===\n";

    // Create a buffer
    flatbuffers::FlatBufferBuilder builder(256);
    auto input = builder.CreateString("Zero-copy test string");
    auto request = CreateStringReversalRequest(builder, input);
    builder.Finish(request);

    // Get raw buffer (would be received over network)
    auto buffer_ptr = builder.GetBufferPointer();
    auto buffer_size = builder.GetSize();

    std::cout << "  Buffer size: " << buffer_size << " bytes\n";

    // Access data without copying - the string pointer points directly into buffer
    auto parsed = flatbuffers::GetRoot<StringReversalRequest>(buffer_ptr);
    const char* str_ptr = parsed->input()->c_str();

    // Verify the pointer is within the buffer range (zero-copy)
    bool is_zero_copy = (reinterpret_cast<const uint8_t*>(str_ptr) >= buffer_ptr) &&
                        (reinterpret_cast<const uint8_t*>(str_ptr) < buffer_ptr + buffer_size);

    std::cout << "  String access is zero-copy: " << (is_zero_copy ? "yes" : "no") << "\n";
    assert(is_zero_copy);
    std::cout << "  Zero-copy test passed!\n";
}

/**
 * @brief Test buffer size efficiency.
 */
void test_buffer_sizes() {
    std::cout << "\n=== Testing Buffer Sizes ===\n";

    // Small message
    {
        flatbuffers::FlatBufferBuilder builder(64);
        auto req = CreateMathOperationRequest(builder, 1.0, 2.0, MathOperation_Add);
        builder.Finish(req);
        std::cout << "  MathOperationRequest size: " << builder.GetSize() << " bytes\n";
    }

    // String message
    {
        flatbuffers::FlatBufferBuilder builder(256);
        auto input = builder.CreateString("Hello, FlatBuffers!");
        auto req = CreateStringReversalRequest(builder, input);
        builder.Finish(req);
        std::cout << "  StringReversalRequest (short): " << builder.GetSize() << " bytes\n";
    }

    // Longer string message
    {
        flatbuffers::FlatBufferBuilder builder(1024);
        std::string long_input(500, 'x');
        auto input = builder.CreateString(long_input);
        auto req = CreateStringReversalRequest(builder, input);
        builder.Finish(req);
        std::cout << "  StringReversalRequest (500 chars): " << builder.GetSize() << " bytes\n";
    }
}

/**
 * @brief Demonstrates direct write access to FlatBuffers using CreateUninitializedVector.
 *
 * This is the key pattern for zero-copy WRITE access:
 * 1. Call CreateUninitializedVector() to allocate space in the buffer
 * 2. Get back a raw pointer to the uninitialized memory
 * 3. Write data directly to that pointer (task generator writes here)
 * 4. Continue building the rest of the message
 *
 * This avoids copying data from an intermediate std::vector into the buffer.
 */
void test_vector_math_direct_write() {
    std::cout << "\n=== Testing Vector Math with Direct Write Access ===\n";

    const size_t VECTOR_SIZE = 1000;

    // Calculate buffer size needed:
    // - VectorMathRequest table overhead: ~32 bytes
    // - Two double vectors: 2 * VECTOR_SIZE * sizeof(double)
    // - Alignment padding: ~16 bytes
    size_t estimated_size = 64 + 2 * VECTOR_SIZE * sizeof(double);

    flatbuffers::FlatBufferBuilder builder(estimated_size);

    // =========================================================================
    // KEY API: CreateUninitializedVector
    // =========================================================================
    // This allocates space for a vector in the buffer and returns:
    // 1. An Offset to use when building the table
    // 2. A raw pointer (via output parameter) for direct writes
    //
    // Signature: CreateUninitializedVector(size_t len, T** buf)
    // =========================================================================

    double* operand_a_ptr = nullptr;
    double* operand_b_ptr = nullptr;

    // IMPORTANT: In FlatBuffers, you must create objects in REVERSE order
    // (children before parents, last fields before first fields)
    // So we create operand_b first, then operand_a

    // Allocate operand_b vector - get direct write pointer
    auto operand_b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &operand_b_ptr);

    // Allocate operand_a vector - get direct write pointer
    auto operand_a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &operand_a_ptr);

    // =========================================================================
    // DIRECT WRITE: Task generator writes directly into buffer memory
    // =========================================================================
    // At this point, operand_a_ptr and operand_b_ptr point directly into
    // the FlatBufferBuilder's internal buffer. We can write to them without
    // any intermediate copies!

    std::cout << "  Writing " << VECTOR_SIZE << " elements directly to buffer...\n";

    // Simulate task generator filling in operand values
    // In real code, this could be:
    // - Reading from a file directly into the buffer
    // - Receiving network data directly into the buffer
    // - Computing values and storing them without intermediate storage
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        operand_a_ptr[i] = static_cast<double>(i);           // [0, 1, 2, 3, ...]
        operand_b_ptr[i] = static_cast<double>(i) * 2.0;     // [0, 2, 4, 6, ...]
    }

    // Now build the table using the pre-allocated vector offsets
    auto request = CreateVectorMathRequest(
        builder,
        operand_a_offset,  // Uses the already-written vector
        operand_b_offset,  // Uses the already-written vector
        MathOperation_Add
    );
    builder.Finish(request);

    std::cout << "  Buffer size: " << builder.GetSize() << " bytes\n";
    std::cout << "  Expected data size: " << (2 * VECTOR_SIZE * sizeof(double)) << " bytes\n";
    std::cout << "  Overhead: " << (builder.GetSize() - 2 * VECTOR_SIZE * sizeof(double)) << " bytes\n";

    // =========================================================================
    // Verify: Parse and check the data (zero-copy read)
    // =========================================================================
    auto parsed = flatbuffers::GetRoot<VectorMathRequest>(builder.GetBufferPointer());

    // Zero-copy read access
    auto vec_a = parsed->operand_a();
    auto vec_b = parsed->operand_b();

    assert(vec_a->size() == VECTOR_SIZE);
    assert(vec_b->size() == VECTOR_SIZE);

    // Verify a few values
    assert(vec_a->Get(0) == 0.0);
    assert(vec_a->Get(999) == 999.0);
    assert(vec_b->Get(0) == 0.0);
    assert(vec_b->Get(999) == 1998.0);

    std::cout << "  Verification: vec_a[999]=" << vec_a->Get(999)
              << ", vec_b[999]=" << vec_b->Get(999) << "\n";
    std::cout << "  Direct write test passed!\n";
}

/**
 * @brief Complete vector math workflow showing direct write for both request and response.
 */
void test_vector_math_full_workflow() {
    std::cout << "\n=== Testing Vector Math Full Workflow (Direct Write) ===\n";

    const size_t VECTOR_SIZE = 100;

    // =========================================================================
    // SENDER SIDE: Create request with direct write access
    // =========================================================================
    flatbuffers::FlatBufferBuilder request_builder(64 + 2 * VECTOR_SIZE * sizeof(double));

    double* req_a_ptr = nullptr;
    double* req_b_ptr = nullptr;

    // Allocate vectors (reverse order)
    auto req_b_offset = request_builder.CreateUninitializedVector(VECTOR_SIZE, &req_b_ptr);
    auto req_a_offset = request_builder.CreateUninitializedVector(VECTOR_SIZE, &req_a_ptr);

    // Direct write: fill operand vectors
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        req_a_ptr[i] = static_cast<double>(i + 1);      // [1, 2, 3, ...]
        req_b_ptr[i] = static_cast<double>(i + 1) * 10; // [10, 20, 30, ...]
    }

    auto request = CreateVectorMathRequest(
        request_builder, req_a_offset, req_b_offset, MathOperation_Multiply
    );
    request_builder.Finish(request);

    // Simulate network transmission
    std::vector<uint8_t> request_bytes(
        request_builder.GetBufferPointer(),
        request_builder.GetBufferPointer() + request_builder.GetSize()
    );

    std::cout << "  Request size: " << request_bytes.size() << " bytes\n";

    // =========================================================================
    // RECEIVER SIDE: Parse request (zero-copy read) and create response (direct write)
    // =========================================================================
    auto parsed_request = flatbuffers::GetRoot<VectorMathRequest>(request_bytes.data());

    // Zero-copy read of operands
    auto vec_a = parsed_request->operand_a();
    auto vec_b = parsed_request->operand_b();
    auto operation = parsed_request->operation();

    size_t result_size = vec_a->size();

    // Create response with direct write access
    flatbuffers::FlatBufferBuilder response_builder(64 + result_size * sizeof(double));

    double* result_ptr = nullptr;
    auto result_offset = response_builder.CreateUninitializedVector(result_size, &result_ptr);

    // Direct write: compute results directly into response buffer
    for (size_t i = 0; i < result_size; ++i) {
        double a = vec_a->Get(i);
        double b = vec_b->Get(i);

        switch (operation) {
            case MathOperation_Add:      result_ptr[i] = a + b; break;
            case MathOperation_Subtract: result_ptr[i] = a - b; break;
            case MathOperation_Multiply: result_ptr[i] = a * b; break;
            case MathOperation_Divide:   result_ptr[i] = (b != 0) ? a / b : 0; break;
        }
    }

    auto response = CreateVectorMathResponse(response_builder, result_offset);
    response_builder.Finish(response);

    std::cout << "  Response size: " << response_builder.GetSize() << " bytes\n";

    // =========================================================================
    // Verify results
    // =========================================================================
    auto parsed_response = flatbuffers::GetRoot<VectorMathResponse>(
        response_builder.GetBufferPointer()
    );

    auto result_vec = parsed_response->result();
    assert(result_vec->size() == VECTOR_SIZE);

    // Verify: (i+1) * (i+1)*10 = (i+1)^2 * 10
    // result[0] = 1 * 10 = 10
    // result[99] = 100 * 1000 = 100000
    assert(result_vec->Get(0) == 10.0);
    assert(result_vec->Get(99) == 100000.0);

    std::cout << "  Verification: result[0]=" << result_vec->Get(0)
              << ", result[99]=" << result_vec->Get(99) << "\n";
    std::cout << "  Full workflow test passed!\n";
}

/**
 * @brief Demonstrates reusing a pre-sized builder for multiple messages.
 *
 * For high-throughput scenarios, you can reuse a FlatBufferBuilder
 * to avoid repeated memory allocations.
 */
void test_builder_reuse() {
    std::cout << "\n=== Testing Builder Reuse Pattern ===\n";

    const size_t VECTOR_SIZE = 50;
    const int NUM_ITERATIONS = 3;

    // Pre-allocate a builder large enough for our messages
    flatbuffers::FlatBufferBuilder builder(64 + 2 * VECTOR_SIZE * sizeof(double));

    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Clear the builder for reuse (keeps the allocated memory)
        builder.Clear();

        double* a_ptr = nullptr;
        double* b_ptr = nullptr;

        auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
        auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

        // Direct write with iteration-specific values
        for (size_t i = 0; i < VECTOR_SIZE; ++i) {
            a_ptr[i] = static_cast<double>(iter * 100 + i);
            b_ptr[i] = static_cast<double>(iter * 100 + i) * 0.5;
        }

        auto request = CreateVectorMathRequest(builder, a_offset, b_offset, MathOperation_Add);
        builder.Finish(request);

        // Verify this iteration
        auto parsed = flatbuffers::GetRoot<VectorMathRequest>(builder.GetBufferPointer());
        assert(parsed->operand_a()->Get(0) == static_cast<double>(iter * 100));

        std::cout << "  Iteration " << iter << ": a[0]=" << parsed->operand_a()->Get(0)
                  << ", buffer size=" << builder.GetSize() << " bytes\n";
    }

    std::cout << "  Builder reuse test passed!\n";
}

/**
 * @brief Demonstrates the "buffer template" pattern - build once, overwrite many times.
 *
 * This is the MOST EFFICIENT pattern when:
 * - Vector sizes are fixed and known in advance
 * - Only the data values change between iterations
 * - Structure (fields, types) remains identical
 *
 * Benefits:
 * - Zero allocations after initial setup
 * - No rebuilding of vtables, offsets, or metadata
 * - Just raw memory writes to update data
 */
void test_buffer_template_pattern() {
    std::cout << "\n=== Testing Buffer Template Pattern (Most Efficient) ===\n";

    const size_t VECTOR_SIZE = 50;
    const int NUM_ITERATIONS = 5;

    // =========================================================================
    // SETUP PHASE: Build the buffer structure ONCE
    // =========================================================================
    flatbuffers::FlatBufferBuilder builder(64 + 2 * VECTOR_SIZE * sizeof(double));

    double* a_ptr = nullptr;
    double* b_ptr = nullptr;

    // Create vectors and get direct-write pointers
    auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
    auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

    // Build the table structure
    auto request = CreateVectorMathRequest(builder, a_offset, b_offset, MathOperation_Multiply);
    builder.Finish(request);

    // Record the buffer info (these remain constant!)
    const uint8_t* buffer_ptr = builder.GetBufferPointer();
    const size_t buffer_size = builder.GetSize();

    std::cout << "  Buffer structure created once: " << buffer_size << " bytes\n";
    std::cout << "  a_ptr address: " << static_cast<void*>(a_ptr) << "\n";
    std::cout << "  b_ptr address: " << static_cast<void*>(b_ptr) << "\n";

    // =========================================================================
    // ITERATION PHASE: Just overwrite data, no rebuilding!
    // =========================================================================
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // NO Clear()! NO CreateUninitializedVector()! NO CreateVectorMathRequest()! NO Finish()!
        // Just write directly to the existing buffer locations:

        for (size_t i = 0; i < VECTOR_SIZE; ++i) {
            a_ptr[i] = static_cast<double>(iter + 1);        // All elements = iter+1
            b_ptr[i] = static_cast<double>(i + 1);           // Elements = 1, 2, 3, ...
        }

        // Buffer is immediately ready to use/send - no additional calls needed!
        // In real code: send(buffer_ptr, buffer_size);

        // Verify the data changed
        auto parsed = flatbuffers::GetRoot<VectorMathRequest>(buffer_ptr);
        double expected_a0 = static_cast<double>(iter + 1);
        double actual_a0 = parsed->operand_a()->Get(0);

        assert(actual_a0 == expected_a0);
        assert(parsed->operand_b()->Get(49) == 50.0);

        std::cout << "  Iteration " << iter << ": a[0]=" << actual_a0
                  << ", b[49]=" << parsed->operand_b()->Get(49) << "\n";
    }

    // Verify pointers haven't changed (same memory locations throughout)
    std::cout << "  a_ptr still at: " << static_cast<void*>(a_ptr) << " (unchanged)\n";
    std::cout << "  Buffer template test passed!\n";
}

/**
 * @brief Demonstrates updating the operation field in a buffer template.
 *
 * For scalar fields in the table (not vectors), you need to use FlatBuffers'
 * mutation API or rebuild. But vector DATA can be overwritten directly.
 */
void test_buffer_template_with_operation_change() {
    std::cout << "\n=== Testing Buffer Template with Scalar Field Updates ===\n";

    const size_t VECTOR_SIZE = 10;

    flatbuffers::FlatBufferBuilder builder(256);

    double* a_ptr = nullptr;
    double* b_ptr = nullptr;

    auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
    auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

    // Initialize data
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        a_ptr[i] = 10.0;
        b_ptr[i] = 2.0;
    }

    auto request = CreateVectorMathRequest(builder, a_offset, b_offset, MathOperation_Add);
    builder.Finish(request);

    // Parse and check initial operation
    auto parsed = flatbuffers::GetRoot<VectorMathRequest>(builder.GetBufferPointer());
    std::cout << "  Initial operation: " << static_cast<int>(parsed->operation()) 
              << " (Add=0)\n";

    // To change a scalar field like 'operation', you have two options:
    // 
    // Option 1: Use mutable API (if you generated with --gen-mutable)
    //           auto mutable_req = GetMutableVectorMathRequest(buf);
    //           mutable_req->mutate_operation(MathOperation_Multiply);
    //
    // Option 2: Rebuild the structure (what we do in test_builder_reuse)
    //
    // For THIS test, we just show that vector DATA can still be updated:

    // Change vector data without rebuilding
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        a_ptr[i] = 100.0;  // Changed from 10.0
        b_ptr[i] = 5.0;    // Changed from 2.0
    }

    // Re-parse and verify data changed but structure intact
    auto reparsed = flatbuffers::GetRoot<VectorMathRequest>(builder.GetBufferPointer());
    assert(reparsed->operand_a()->Get(0) == 100.0);
    assert(reparsed->operand_b()->Get(0) == 5.0);
    assert(reparsed->operation() == MathOperation_Add);  // Operation unchanged

    std::cout << "  After data update: a[0]=" << reparsed->operand_a()->Get(0)
              << ", b[0]=" << reparsed->operand_b()->Get(0)
              << ", operation=" << static_cast<int>(reparsed->operation()) << "\n";
    std::cout << "  Buffer template with scalar test passed!\n";
}

/**
 * @brief Demonstrates Fused Multiply-Add (a + c*b) with buffer template pattern.
 *
 * Key insight: Store the scalar c as a SINGLE-ELEMENT VECTOR [double].
 * This gives you a double* pointer just like the vectors, allowing direct
 * updates without rebuilding the buffer or using mutable API.
 *
 * Schema approach:
 *   scalar_c: [double];  // NOT scalar_c: double;
 *
 * This way:
 *   - a_ptr points to vector a data
 *   - b_ptr points to vector b data
 *   - c_ptr points to the single scalar value (as a 1-element array)
 *
 * All three can be updated with simple pointer writes!
 */
void test_fused_multiply_add_buffer_template() {
    std::cout << "\n=== Testing Fused Multiply-Add with Buffer Template ===\n";
    std::cout << "  Operation: result[i] = a[i] + c * b[i]\n\n";

    const size_t VECTOR_SIZE = 10;
    const int NUM_ITERATIONS = 4;

    // =========================================================================
    // SETUP PHASE: Build the buffer structure ONCE
    // =========================================================================
    flatbuffers::FlatBufferBuilder builder(256 + 2 * VECTOR_SIZE * sizeof(double));

    double* a_ptr = nullptr;
    double* b_ptr = nullptr;
    double* c_ptr = nullptr;  // Single scalar, but accessed via pointer!

    // Create vectors in reverse order (FlatBuffers requirement)
    // scalar_c is a 1-element vector
    auto c_offset = builder.CreateUninitializedVector(1, &c_ptr);
    auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
    auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

    // Initialize with some values
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        a_ptr[i] = 1.0;
        b_ptr[i] = 1.0;
    }
    c_ptr[0] = 1.0;  // Scalar c accessed as c_ptr[0]

    // Build the table structure
    auto request = CreateFusedMultiplyAddRequest(builder, a_offset, b_offset, c_offset);
    builder.Finish(request);

    const uint8_t* buffer_ptr = builder.GetBufferPointer();
    const size_t buffer_size = builder.GetSize();

    std::cout << "  Buffer created: " << buffer_size << " bytes\n";
    std::cout << "  a_ptr: " << static_cast<void*>(a_ptr) << "\n";
    std::cout << "  b_ptr: " << static_cast<void*>(b_ptr) << "\n";
    std::cout << "  c_ptr: " << static_cast<void*>(c_ptr) << " (scalar as 1-element vector)\n\n";

    // =========================================================================
    // ITERATION PHASE: Just overwrite a, b, and c - no rebuilding!
    // =========================================================================
    std::cout << "  Testing different values of c (scalar multiplier):\n";

    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Update ALL data with simple pointer writes:
        
        // Update vectors
        for (size_t i = 0; i < VECTOR_SIZE; ++i) {
            a_ptr[i] = static_cast<double>(i);       // a = [0, 1, 2, 3, ...]
            b_ptr[i] = 2.0;                          // b = [2, 2, 2, 2, ...]
        }

        // Update scalar c - just write to c_ptr[0]!
        double c_value = static_cast<double>(iter + 1);  // c = 1, 2, 3, 4
        c_ptr[0] = c_value;

        // Buffer is ready to use/send immediately - no Finish() needed!

        // Parse and compute result
        auto parsed = flatbuffers::GetRoot<FusedMultiplyAddRequest>(buffer_ptr);
        auto vec_a = parsed->operand_a();
        auto vec_b = parsed->operand_b();
        double c = parsed->scalar_c()->Get(0);

        // Compute a + c*b for element [5]
        // a[5] = 5, b[5] = 2, c = iter+1
        // result = 5 + (iter+1) * 2
        double result_5 = vec_a->Get(5) + c * vec_b->Get(5);
        double expected = 5.0 + c_value * 2.0;

        assert(result_5 == expected);

        std::cout << "    c=" << c << ": a[5] + c*b[5] = "
                  << vec_a->Get(5) << " + " << c << "*" << vec_b->Get(5)
                  << " = " << result_5 << "\n";
    }

    std::cout << "\n  All pointers unchanged (no reallocation occurred):\n";
    std::cout << "  a_ptr: " << static_cast<void*>(a_ptr) << "\n";
    std::cout << "  b_ptr: " << static_cast<void*>(b_ptr) << "\n";
    std::cout << "  c_ptr: " << static_cast<void*>(c_ptr) << "\n";
    std::cout << "  Fused multiply-add buffer template test passed!\n";
}

/**
 * @brief Alternative: Using mutable API for true scalar fields.
 *
 * If you prefer scalar_c as a true scalar (not a vector), you need:
 * 1. Generate with: flatc --gen-mutable --cpp skill_task.fbs
 * 2. Use the mutate_*() methods
 *
 * Example (if schema had `scalar_c: double;`):
 *   auto mutable_req = GetMutableFusedMultiplyAddRequest(buffer);
 *   mutable_req->mutate_scalar_c(new_value);
 *
 * Trade-offs:
 * - True scalar: Slightly smaller buffer, requires mutable API
 * - Single-element vector: +8 bytes overhead, but uniform pointer access
 */
void show_mutable_api_alternative() {
    std::cout << "\n=== Note: Mutable API Alternative ===\n";
    std::cout << "  For true scalar fields, you can use FlatBuffers' mutable API:\n";
    std::cout << "    1. Generate with: flatc --gen-mutable --cpp schema.fbs\n";
    std::cout << "    2. Use mutate_*() methods: mutable_req->mutate_scalar_c(value);\n";
    std::cout << "  \n";
    std::cout << "  Trade-offs:\n";
    std::cout << "    - True scalar (double): Smaller, needs mutable API generation\n";
    std::cout << "    - 1-element vector [double]: +8 bytes, uniform pointer pattern\n";
    std::cout << "  \n";
    std::cout << "  For buffer template pattern, single-element vector is often cleaner.\n";
}

/**
 * @brief Demonstrates Fused Multiply-Add with TRUE scalar using mutable API.
 *
 * This uses the --gen-mutable generated code to update a true scalar field
 * without rebuilding the buffer. The mutable API provides mutate_*() methods.
 *
 * Schema:
 *   scalar_c: double;  // TRUE scalar (not a vector)
 *
 * Generated API:
 *   mutate_scalar_c(double value) -> bool
 *
 * Benefits:
 *   - No wasted space (scalar is 8 bytes, not 8 + vector overhead)
 *   - Semantic clarity (it's clearly a scalar in the schema)
 *
 * Requirement:
 *   - Must generate with: flatc --gen-mutable --cpp schema.fbs
 */
void test_fma_mutable_scalar_buffer_template() {
    std::cout << "\n=== Testing FMA with Mutable Scalar (Buffer Template) ===\n";
    std::cout << "  Operation: result[i] = a[i] + c * b[i]\n";
    std::cout << "  Using mutate_scalar_c() for true scalar field\n\n";

    const size_t VECTOR_SIZE = 10;
    const int NUM_ITERATIONS = 4;

    // =========================================================================
    // SETUP PHASE: Build the buffer structure ONCE
    // =========================================================================
    flatbuffers::FlatBufferBuilder builder(256 + 2 * VECTOR_SIZE * sizeof(double));

    double* a_ptr = nullptr;
    double* b_ptr = nullptr;

    // Create vectors in reverse order
    auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
    auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

    // Initialize vectors
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        a_ptr[i] = static_cast<double>(i);  // a = [0, 1, 2, 3, ...]
        b_ptr[i] = 2.0;                      // b = [2, 2, 2, 2, ...]
    }

    // Initial scalar value
    double initial_c = 1.0;

    // Build the table with the scalar field
    auto request = CreateFusedMultiplyAddMutableRequest(
        builder, a_offset, b_offset, initial_c
    );
    builder.Finish(request);

    // Get buffer pointer - this stays constant
    uint8_t* buffer_ptr = builder.GetBufferPointer();
    const size_t buffer_size = builder.GetSize();

    std::cout << "  Buffer created: " << buffer_size << " bytes\n";
    std::cout << "  a_ptr: " << static_cast<void*>(a_ptr) << "\n";
    std::cout << "  b_ptr: " << static_cast<void*>(b_ptr) << "\n\n";

    // =========================================================================
    // ITERATION PHASE: Update vectors via pointers, scalar via mutate_*()
    // =========================================================================
    std::cout << "  Testing different values of c using mutate_scalar_c():\n";

    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // Update vectors directly via pointers (same as before)
        for (size_t i = 0; i < VECTOR_SIZE; ++i) {
            a_ptr[i] = static_cast<double>(i);
            b_ptr[i] = 2.0;
        }

        // =====================================================================
        // KEY: Update scalar using mutable API
        // =====================================================================
        // flatbuffers::GetMutableRoot<T>() returns a mutable pointer
        // that allows modifying scalar fields in-place via mutate_*()
        double new_c = static_cast<double>(iter + 1);

        auto mutable_request = flatbuffers::GetMutableRoot<FusedMultiplyAddMutableRequest>(buffer_ptr);
        bool success = mutable_request->mutate_scalar_c(new_c);

        if (!success) {
            std::cerr << "  ERROR: mutate_scalar_c() failed!\n";
            return;
        }

        // Buffer is ready to use/send immediately!

        // Verify by parsing (read-only view)
        auto parsed = flatbuffers::GetRoot<FusedMultiplyAddMutableRequest>(buffer_ptr);
        double c = parsed->scalar_c();

        // Compute a + c*b for element [5]
        double result_5 = parsed->operand_a()->Get(5) + c * parsed->operand_b()->Get(5);
        double expected = 5.0 + new_c * 2.0;

        assert(result_5 == expected);
        assert(c == new_c);

        std::cout << "    mutate_scalar_c(" << new_c << "): a[5] + c*b[5] = "
                  << parsed->operand_a()->Get(5) << " + " << c << "*" << parsed->operand_b()->Get(5)
                  << " = " << result_5 << "\n";
    }

    std::cout << "\n  Pointers unchanged (no reallocation):\n";
    std::cout << "  a_ptr: " << static_cast<void*>(a_ptr) << "\n";
    std::cout << "  b_ptr: " << static_cast<void*>(b_ptr) << "\n";
    std::cout << "  FMA mutable scalar buffer template test passed!\n";
}

/**
 * @brief Compare the two approaches side-by-side.
 */
void compare_scalar_approaches() {
    std::cout << "\n=== Comparing Scalar Approaches for Buffer Template ===\n\n";

    const size_t VECTOR_SIZE = 100;

    // Approach 1: Single-element vector
    {
        flatbuffers::FlatBufferBuilder builder(256 + 2 * VECTOR_SIZE * sizeof(double));
        double* a_ptr = nullptr;
        double* b_ptr = nullptr;
        double* c_ptr = nullptr;

        auto c_offset = builder.CreateUninitializedVector(1, &c_ptr);
        auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
        auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

        for (size_t i = 0; i < VECTOR_SIZE; ++i) { a_ptr[i] = 1.0; b_ptr[i] = 1.0; }
        c_ptr[0] = 1.0;

        auto req = CreateFusedMultiplyAddRequest(builder, a_offset, b_offset, c_offset);
        builder.Finish(req);

        std::cout << "  Approach 1: scalar_c as [double] (1-element vector)\n";
        std::cout << "    Buffer size: " << builder.GetSize() << " bytes\n";
        std::cout << "    Update method: c_ptr[0] = new_value;\n\n";
    }

    // Approach 2: True scalar with mutable API
    {
        flatbuffers::FlatBufferBuilder builder(256 + 2 * VECTOR_SIZE * sizeof(double));
        double* a_ptr = nullptr;
        double* b_ptr = nullptr;

        auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
        auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

        for (size_t i = 0; i < VECTOR_SIZE; ++i) { a_ptr[i] = 1.0; b_ptr[i] = 1.0; }

        auto req = CreateFusedMultiplyAddMutableRequest(builder, a_offset, b_offset, 1.0);
        builder.Finish(req);

        std::cout << "  Approach 2: scalar_c as double (true scalar, mutable API)\n";
        std::cout << "    Buffer size: " << builder.GetSize() << " bytes\n";
        std::cout << "    Update method: mutable_req->mutate_scalar_c(new_value);\n\n";
    }

    std::cout << "  Summary:\n";
    std::cout << "    - 1-element vector: Uniform pointer access, slightly larger\n";
    std::cout << "    - True scalar + mutable: Smaller, requires GetMutable*() call\n";
    std::cout << "    - Both allow buffer template pattern without rebuilding!\n";
}

/**
 * @brief Portable FMA buffer template using WriteScalar and mutable scalar API.
 *
 * This is the RECOMMENDED pattern for maximum portability:
 * 1. Use flatbuffers::WriteScalar() for vector element writes (handles endianness)
 * 2. Use mutate_scalar_c() for true scalar updates (generated mutable API)
 *
 * Performance notes:
 * - On little-endian (x86, x64, most ARM): WriteScalar is a no-op assignment
 * - On big-endian: WriteScalar performs necessary byte swap
 * - mutate_scalar_c() writes directly to a known offset - minimal overhead
 *
 * This ensures the buffer is valid on ANY architecture without code changes.
 */
void test_portable_fma_buffer_template() {
    std::cout << "\n=== Testing Portable FMA Buffer Template ===\n";
    std::cout << "  Using WriteScalar() for vectors + mutate_scalar_c() for scalar\n";
    std::cout << "  Operation: result[i] = a[i] + c * b[i]\n\n";

    const size_t VECTOR_SIZE = 10;
    const int NUM_ITERATIONS = 4;

    // =========================================================================
    // SETUP PHASE: Build the buffer structure ONCE
    // =========================================================================
    flatbuffers::FlatBufferBuilder builder(256 + 2 * VECTOR_SIZE * sizeof(double));

    double* a_ptr = nullptr;
    double* b_ptr = nullptr;

    // Create vectors in reverse order
    auto b_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &b_ptr);
    auto a_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &a_ptr);

    // Initialize vectors using WriteScalar for portability
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        flatbuffers::WriteScalar(&a_ptr[i], static_cast<double>(i));
        flatbuffers::WriteScalar(&b_ptr[i], 2.0);
    }

    // Initial scalar value
    double initial_c = 1.0;

    // Build the table with the true scalar field
    auto request = CreateFusedMultiplyAddMutableRequest(
        builder, a_offset, b_offset, initial_c
    );
    builder.Finish(request);

    // Get buffer pointer - this stays constant
    uint8_t* buffer_ptr = builder.GetBufferPointer();
    const size_t buffer_size = builder.GetSize();

    std::cout << "  Buffer created: " << buffer_size << " bytes\n";
    std::cout << "  Endianness: " << (FLATBUFFERS_LITTLEENDIAN ? "little" : "big") << "\n";
    std::cout << "  WriteScalar overhead on this platform: "
              << (FLATBUFFERS_LITTLEENDIAN ? "none (no-op)" : "byte swap") << "\n\n";

    // =========================================================================
    // ITERATION PHASE: Update vectors with WriteScalar, scalar with mutate_*()
    // =========================================================================
    std::cout << "  Running iterations with portable writes:\n";

    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        // =====================================================================
        // PORTABLE VECTOR WRITES: Use WriteScalar for each element
        // =====================================================================
        for (size_t i = 0; i < VECTOR_SIZE; ++i) {
            // WriteScalar handles endian conversion automatically
            flatbuffers::WriteScalar(&a_ptr[i], static_cast<double>(i));
            flatbuffers::WriteScalar(&b_ptr[i], 2.0);
        }

        // =====================================================================
        // PORTABLE SCALAR UPDATE: Use mutable API
        // =====================================================================
        double new_c = static_cast<double>(iter + 1);
        auto mutable_request = flatbuffers::GetMutableRoot<FusedMultiplyAddMutableRequest>(buffer_ptr);
        bool mutate_ok = mutable_request->mutate_scalar_c(new_c);
        assert(mutate_ok);

        // Buffer is now ready to send - properly formatted for any architecture!

        // Verify by parsing (using ReadScalar internally via Get())
        auto parsed = flatbuffers::GetRoot<FusedMultiplyAddMutableRequest>(buffer_ptr);
        double c = parsed->scalar_c();

        // Get() uses ReadScalar internally, so this is also portable
        double a5 = parsed->operand_a()->Get(5);
        double b5 = parsed->operand_b()->Get(5);
        double result_5 = a5 + c * b5;
        double expected = 5.0 + new_c * 2.0;

        assert(result_5 == expected);

        std::cout << "    Iter " << iter << ": a[5]=" << a5 << ", b[5]=" << b5
                  << ", c=" << c << " -> result=" << result_5 << "\n";
    }

    std::cout << "\n  Portable FMA buffer template test passed!\n";
}

/**
 * @brief Benchmark comparing direct writes vs WriteScalar.
 *
 * On little-endian systems, these should be nearly identical.
 * This demonstrates that portability doesn't cost performance.
 */
void benchmark_write_scalar_overhead() {
    std::cout << "\n=== Benchmarking WriteScalar Overhead ===\n";

    const size_t VECTOR_SIZE = 10000;
    const int NUM_ITERATIONS = 100;

    flatbuffers::FlatBufferBuilder builder(64 + VECTOR_SIZE * sizeof(double));

    double* data_ptr = nullptr;
    auto data_offset = builder.CreateUninitializedVector(VECTOR_SIZE, &data_ptr);
    (void)data_offset;  // Suppress unused warning

    // Warm up
    for (size_t i = 0; i < VECTOR_SIZE; ++i) {
        data_ptr[i] = static_cast<double>(i);
    }

    // Method 1: Direct assignment
    auto start1 = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (size_t i = 0; i < VECTOR_SIZE; ++i) {
            data_ptr[i] = static_cast<double>(i + iter);
        }
    }
    auto end1 = std::chrono::high_resolution_clock::now();
    auto direct_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end1 - start1).count();

    // Method 2: WriteScalar (portable)
    auto start2 = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < NUM_ITERATIONS; ++iter) {
        for (size_t i = 0; i < VECTOR_SIZE; ++i) {
            flatbuffers::WriteScalar(&data_ptr[i], static_cast<double>(i + iter));
        }
    }
    auto end2 = std::chrono::high_resolution_clock::now();
    auto portable_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end2 - start2).count();

    double ratio = static_cast<double>(portable_ns) / static_cast<double>(direct_ns);

    std::cout << "  Vector size: " << VECTOR_SIZE << ", Iterations: " << NUM_ITERATIONS << "\n";
    std::cout << "  Direct assignment: " << direct_ns / 1000000.0 << " ms\n";
    std::cout << "  WriteScalar:       " << portable_ns / 1000000.0 << " ms\n";
    std::cout << "  Ratio (portable/direct): " << ratio << "x\n";

    if (FLATBUFFERS_LITTLEENDIAN) {
        std::cout << "  (On little-endian, WriteScalar compiles to direct assignment)\n";
    }

    // They should be very close (within 20% typically, often identical)
    std::cout << "  Benchmark complete - portability has minimal overhead!\n";
}

}  // namespace

int main() {
    std::cout << "=== FlatBuffers Skill Serialization Test ===\n\n";

    // Test 1: String reversal round-trip (full skill workflow)
    std::cout << "=== Testing String Reversal Skill ===\n";
    auto request = create_string_reversal_request(1001, "Hello, Task Messenger!");
    std::cout << "Request size: " << request.size() << " bytes\n";

    auto response = process_skill_request(request);
    std::cout << "Response size: " << response.size() << " bytes\n";

    // Parse and verify response using flatbuffers::GetRoot<T>
    auto skill_response = flatbuffers::GetRoot<SkillResponse>(response.data());
    assert(skill_response->success());
    assert(skill_response->task_id() == 1001);

    auto response_payload = skill_response->payload();
    auto inner_response = flatbuffers::GetRoot<StringReversalResponse>(response_payload->data());
    std::cout << "Verified response: \"" << inner_response->output()->str() << "\"\n";
    assert(inner_response->output()->str() == "!regnesseM ksaT ,olleH");
    std::cout << "String reversal test passed!\n";

    // Test 2: Math operation (scalar)
    test_math_operation();

    // Test 3: Zero-copy read access
    test_zero_copy_access();

    // Test 4: Buffer sizes
    test_buffer_sizes();

    // Test 5: Vector math with direct write access (zero-copy write)
    test_vector_math_direct_write();

    // Test 6: Full vector math workflow (direct write for both request and response)
    test_vector_math_full_workflow();

    // Test 7: Builder reuse pattern (Clear + rebuild each iteration)
    test_builder_reuse();

    // Test 8: Buffer template pattern (build once, overwrite data only - MOST EFFICIENT)
    test_buffer_template_pattern();

    // Test 9: Buffer template with scalar field considerations
    test_buffer_template_with_operation_change();

    // Test 10: Fused Multiply-Add (a + c*b) with buffer template pattern
    // Demonstrates storing scalar as single-element vector for direct pointer access
    test_fused_multiply_add_buffer_template();

    // Info: Alternative using mutable API
    show_mutable_api_alternative();

    // Test 11: FMA with mutable scalar (true scalar with mutate_*() method)
    test_fma_mutable_scalar_buffer_template();

    // Test 12: Compare both scalar approaches
    compare_scalar_approaches();

    // Test 13: Portable FMA buffer template (WriteScalar + mutable scalar)
    // This is the RECOMMENDED pattern for portable high-performance code
    test_portable_fma_buffer_template();

    // Test 14: Benchmark WriteScalar overhead (should be negligible on little-endian)
    benchmark_write_scalar_overhead();

    std::cout << "\n=== All FlatBuffers tests passed! ===\n";
    return 0;
}
