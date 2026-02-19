//
// Cap'n Proto Feature Demonstration Tests
//
// This file demonstrates the key features of Cap'n Proto:
// 1. Basic serialization with MallocMessageBuilder (zero-allocation builder)
// 2. Zero-copy reads with FlatArrayMessageReader
// 3. Unions (discriminated variants)
// 4. Lists (arrays of values or structs)
// 5. Nested structures
// 6. Async/Promise patterns with kj::Promise
//

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <capnp/serialize-packed.h>
#include <kj/async.h>
#include <kj/async-io.h>

#include "skill_task.capnp.h"  // Generated from schema

#include <iostream>
#include <vector>
#include <cstring>
#include <chrono>
#include <cassert>

using namespace TaskMessenger::Skills;

//==============================================================================
// Test 1: Basic Serialization/Deserialization
//
// Cap'n Proto uses a "builder" pattern for writing and "reader" pattern for
// reading. The key insight is that Builder allocates and sets values, while
// Reader provides zero-copy access to the serialized data.
//==============================================================================
void test_basic_serialization() {
    std::cout << "=== Test 1: Basic Serialization ===" << std::endl;

    // ===== BUILDING (Serialization) =====
    // MallocMessageBuilder allocates memory for the message.
    // It uses an arena allocator internally for efficiency.
    ::capnp::MallocMessageBuilder message;

    // initRoot<T>() creates the root struct and returns a Builder.
    // Builders are facades that write directly to the message's memory.
    auto request = message.initRoot<SkillRequest>();

    // Set scalar fields
    request.setTaskId(42);

    // Initialize the union variant - only one can be set
    auto mathOp = request.initPayload().initMathOperation();
    mathOp.setOperandA(10.0);
    mathOp.setOperandB(3.0);
    mathOp.setOperation(MathOperation::MULTIPLY);

    // Serialize to bytes (this is how you'd send over network)
    kj::Array<capnp::word> serialized = capnp::messageToFlatArray(message);
    
    // Get raw bytes for transmission
    auto bytes = serialized.asBytes();
    std::cout << "Serialized size: " << bytes.size() << " bytes" << std::endl;

    // ===== READING (Deserialization, Zero-Copy) =====
    // FlatArrayMessageReader reads directly from the byte array.
    // NO COPYING occurs - readers point directly into the buffer.
    kj::ArrayPtr<const capnp::word> words(
        reinterpret_cast<const capnp::word*>(bytes.begin()),
        bytes.size() / sizeof(capnp::word)
    );
    ::capnp::FlatArrayMessageReader reader(words);

    // getRoot<T>() returns a Reader - a view into the serialized data
    auto readRequest = reader.getRoot<SkillRequest>();

    std::cout << "Task ID: " << readRequest.getTaskId() << std::endl;

    // Check which union variant is set
    if (readRequest.getPayload().isMathOperation()) {
        auto op = readRequest.getPayload().getMathOperation();
        std::cout << "Math Operation: " 
                  << op.getOperandA() << " op " << op.getOperandB() 
                  << " = " << (op.getOperandA() * op.getOperandB())
                  << std::endl;
    }

    std::cout << std::endl;
}

//==============================================================================
// Test 2: Union (Discriminated Variants)
//
// Cap'n Proto unions are type-safe variants. The union discriminant is stored
// inline, and you can use which() or is*() methods to check the active variant.
//==============================================================================
void test_unions() {
    std::cout << "=== Test 2: Union Handling ===" << std::endl;

    // Create requests with different skill types
    std::vector<kj::Array<capnp::word>> messages;

    // Message 1: String reversal
    {
        ::capnp::MallocMessageBuilder builder;
        auto req = builder.initRoot<SkillRequest>();
        req.setTaskId(1);
        auto strReq = req.initPayload().initStringReversal();
        strReq.setInput("Hello, Cap'n Proto!");
        messages.push_back(capnp::messageToFlatArray(builder));
    }

    // Message 2: Math operation
    {
        ::capnp::MallocMessageBuilder builder;
        auto req = builder.initRoot<SkillRequest>();
        req.setTaskId(2);
        auto mathReq = req.initPayload().initMathOperation();
        mathReq.setOperandA(100.0);
        mathReq.setOperandB(7.0);
        mathReq.setOperation(MathOperation::DIVIDE);
        messages.push_back(capnp::messageToFlatArray(builder));
    }

    // Read and dispatch based on union type
    for (auto& serialized : messages) {
        auto bytes = serialized.asBytes();
        kj::ArrayPtr<const capnp::word> words(
            reinterpret_cast<const capnp::word*>(bytes.begin()),
            bytes.size() / sizeof(capnp::word)
        );
        ::capnp::FlatArrayMessageReader reader(words);
        auto request = reader.getRoot<SkillRequest>();

        std::cout << "Task " << request.getTaskId() << ": ";

        // Use which() to switch on the discriminant
        switch (request.getPayload().which()) {
            case SkillRequest::Payload::STRING_REVERSAL: {
                auto strReq = request.getPayload().getStringReversal();
                std::cout << "String Reversal: \"" << strReq.getInput().cStr() << "\"" << std::endl;
                break;
            }
            case SkillRequest::Payload::MATH_OPERATION: {
                auto mathReq = request.getPayload().getMathOperation();
                std::cout << "Math: " << mathReq.getOperandA() 
                          << " / " << mathReq.getOperandB() 
                          << " = " << (mathReq.getOperandA() / mathReq.getOperandB())
                          << std::endl;
                break;
            }
            case SkillRequest::Payload::VECTOR_MATH:
                std::cout << "Vector Math" << std::endl;
                break;
            case SkillRequest::Payload::FUSED_MULTIPLY_ADD:
                std::cout << "Fused Multiply-Add" << std::endl;
                break;
        }
    }

    std::cout << std::endl;
}

//==============================================================================
// Test 3: Lists (Arrays)
//
// Cap'n Proto lists are contiguous arrays. For primitive types like Float64,
// they serialize as packed arrays. For structs, each element is inline.
//==============================================================================
void test_lists() {
    std::cout << "=== Test 3: List Handling ===" << std::endl;

    // Create a vector math request
    ::capnp::MallocMessageBuilder builder;
    auto request = builder.initRoot<SkillRequest>();
    request.setTaskId(3);

    auto vecReq = request.initPayload().initVectorMath();
    
    // Initialize lists with size
    auto operandA = vecReq.initOperandA(5);
    auto operandB = vecReq.initOperandB(5);

    // Set list elements
    for (uint32_t i = 0; i < 5; ++i) {
        operandA.set(i, static_cast<double>(i + 1));      // [1, 2, 3, 4, 5]
        operandB.set(i, static_cast<double>(i + 1) * 2);  // [2, 4, 6, 8, 10]
    }
    vecReq.setOperation(MathOperation::ADD);

    // Serialize and read back
    auto serialized = capnp::messageToFlatArray(builder);
    auto bytes = serialized.asBytes();
    std::cout << "Vector request size: " << bytes.size() << " bytes" << std::endl;

    kj::ArrayPtr<const capnp::word> words(
        reinterpret_cast<const capnp::word*>(bytes.begin()),
        bytes.size() / sizeof(capnp::word)
    );
    ::capnp::FlatArrayMessageReader reader(words);
    auto readReq = reader.getRoot<SkillRequest>();

    auto readVec = readReq.getPayload().getVectorMath();
    auto readA = readVec.getOperandA();
    auto readB = readVec.getOperandB();

    std::cout << "Operand A: [";
    for (size_t i = 0; i < readA.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << readA[i];
    }
    std::cout << "]" << std::endl;

    std::cout << "Operand B: [";
    for (size_t i = 0; i < readB.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << readB[i];
    }
    std::cout << "]" << std::endl;

    // Compute result (element-wise addition)
    std::cout << "Result (A + B): [";
    for (size_t i = 0; i < readA.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << (readA[i] + readB[i]);
    }
    std::cout << "]" << std::endl;

    std::cout << std::endl;
}

//==============================================================================
// Test 4: Nested Structures
//
// Cap'n Proto structs can contain other structs. Nested structs are accessed
// via getters that return readers/builders for the nested type.
//==============================================================================
void test_nested_structures() {
    std::cout << "=== Test 4: Nested Structures ===" << std::endl;

    ::capnp::MallocMessageBuilder builder;
    auto taskWithMeta = builder.initRoot<TaskWithMetadata>();

    // Initialize nested metadata
    auto metadata = taskWithMeta.initMetadata();
    metadata.setCreatedAt(1234567890123);
    metadata.setPriority(5);

    // Initialize list of tags
    auto tags = metadata.initTags(3);
    tags.set(0, "urgent");
    tags.set(1, "compute");
    tags.set(2, "math");

    // Initialize nested request
    auto request = taskWithMeta.initRequest();
    request.setTaskId(100);
    auto mathReq = request.initPayload().initMathOperation();
    mathReq.setOperandA(42.0);
    mathReq.setOperandB(2.0);
    mathReq.setOperation(MathOperation::MULTIPLY);

    // Serialize
    auto serialized = capnp::messageToFlatArray(builder);
    auto bytes = serialized.asBytes();
    std::cout << "TaskWithMetadata size: " << bytes.size() << " bytes" << std::endl;

    // Read back
    kj::ArrayPtr<const capnp::word> words(
        reinterpret_cast<const capnp::word*>(bytes.begin()),
        bytes.size() / sizeof(capnp::word)
    );
    ::capnp::FlatArrayMessageReader reader(words);
    auto readTask = reader.getRoot<TaskWithMetadata>();

    auto readMeta = readTask.getMetadata();
    std::cout << "Timestamp: " << readMeta.getCreatedAt() << std::endl;
    std::cout << "Priority: " << static_cast<int>(readMeta.getPriority()) << std::endl;
    
    std::cout << "Tags: [";
    auto readTags = readMeta.getTags();
    for (size_t i = 0; i < readTags.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << "\"" << readTags[i].cStr() << "\"";
    }
    std::cout << "]" << std::endl;

    auto readReq = readTask.getRequest();
    std::cout << "Task ID: " << readReq.getTaskId() << std::endl;

    std::cout << std::endl;
}

//==============================================================================
// Test 5: Batch Processing (List of Structs)
//
// Demonstrates a batch of requests serialized together.
//==============================================================================
void test_batch_processing() {
    std::cout << "=== Test 5: Batch Processing ===" << std::endl;

    ::capnp::MallocMessageBuilder builder;
    auto batch = builder.initRoot<BatchRequest>();

    // Create batch of 3 requests
    auto tasks = batch.initTasks(3);

    // Task 0: String reversal
    tasks[0].setTaskId(100);
    tasks[0].initPayload().initStringReversal().setInput("batch item 0");

    // Task 1: Math operation
    tasks[1].setTaskId(101);
    auto math1 = tasks[1].initPayload().initMathOperation();
    math1.setOperandA(5.0);
    math1.setOperandB(3.0);
    math1.setOperation(MathOperation::ADD);

    // Task 2: Vector math
    tasks[2].setTaskId(102);
    auto vec2 = tasks[2].initPayload().initVectorMath();
    auto a = vec2.initOperandA(2);
    auto b = vec2.initOperandB(2);
    a.set(0, 1.0); a.set(1, 2.0);
    b.set(0, 3.0); b.set(1, 4.0);
    vec2.setOperation(MathOperation::MULTIPLY);

    // Serialize
    auto serialized = capnp::messageToFlatArray(builder);
    auto bytes = serialized.asBytes();
    std::cout << "Batch size: " << bytes.size() << " bytes for " 
              << tasks.size() << " tasks" << std::endl;

    // Read back
    kj::ArrayPtr<const capnp::word> words(
        reinterpret_cast<const capnp::word*>(bytes.begin()),
        bytes.size() / sizeof(capnp::word)
    );
    ::capnp::FlatArrayMessageReader reader(words);
    auto readBatch = reader.getRoot<BatchRequest>();

    std::cout << "Batch contains " << readBatch.getTasks().size() << " tasks:" << std::endl;
    for (auto task : readBatch.getTasks()) {
        std::cout << "  Task " << task.getTaskId() << ": ";
        switch (task.getPayload().which()) {
            case SkillRequest::Payload::STRING_REVERSAL:
                std::cout << "StringReversal" << std::endl;
                break;
            case SkillRequest::Payload::MATH_OPERATION:
                std::cout << "MathOperation" << std::endl;
                break;
            case SkillRequest::Payload::VECTOR_MATH:
                std::cout << "VectorMath" << std::endl;
                break;
            case SkillRequest::Payload::FUSED_MULTIPLY_ADD:
                std::cout << "FusedMultiplyAdd" << std::endl;
                break;
        }
    }

    std::cout << std::endl;
}

//==============================================================================
// Test 6: Async/Promise Pattern
//
// Cap'n Proto includes kj::Promise for async programming. This is the foundation
// for Cap'n Proto RPC, but can be used standalone for async operations.
//
// Key concepts:
// - kj::Promise<T>: A promise that will eventually produce a value of type T
// - kj::READY_NOW: A fulfilled promise (immediate value)
// - promise.then(): Transform the result when it becomes available
// - kj::joinPromises(): Wait for multiple promises
//
// NOTE: Full RPC capabilities require setting up a Cap'n Proto RPC server/client.
// This example shows the promise primitives without network RPC.
//==============================================================================
void test_async_promise() {
    std::cout << "=== Test 6: Async/Promise Pattern ===" << std::endl;

    // Create an async event loop
    // In real applications, this would be your main event loop.
    auto ioContext = kj::setupAsyncIo();

    // A simple promise that resolves immediately
    // kj::READY_NOW is a convenience for creating a fulfilled promise.
    auto immediatePromise = kj::Promise<int>(42);  // Already fulfilled with 42

    // Chain transformations with .then()
    // Each .then() returns a new promise for the transformed value.
    // Note: kj uses kj::String, not std::string
    auto transformedPromise = kj::mv(immediatePromise)
        .then([](int value) {
            std::cout << "  Step 1: Received value " << value << std::endl;
            return value * 2;  // Transform: 42 -> 84
        })
        .then([](int value) {
            std::cout << "  Step 2: Doubled to " << value << std::endl;
            return kj::str(value);  // Transform: 84 -> "84" (as kj::String)
        })
        .then([](kj::String value) {
            std::cout << "  Step 3: Converted to string \"" << value.cStr() << "\"" << std::endl;
            return kj::str("Result: ", value);  // Full result string
        });

    // Wait for the promise to complete
    // In real async code, you'd avoid blocking, but for this demo we wait.
    kj::String result = transformedPromise.wait(ioContext.waitScope);
    std::cout << "  Final: " << result.cStr() << std::endl;

    std::cout << std::endl;

    // Example: Multiple concurrent promises (simulated async tasks)
    std::cout << "Multiple promises example:" << std::endl;

    auto promise1 = kj::Promise<int>(10).then([](int v) {
        std::cout << "  Promise 1 resolving with " << v << std::endl;
        return v;
    });

    auto promise2 = kj::Promise<int>(20).then([](int v) {
        std::cout << "  Promise 2 resolving with " << v << std::endl;
        return v;
    });

    auto promise3 = kj::Promise<int>(30).then([](int v) {
        std::cout << "  Promise 3 resolving with " << v << std::endl;
        return v;
    });

    // Collect all promises into an array
    kj::Vector<kj::Promise<int>> promises;
    promises.add(kj::mv(promise1));
    promises.add(kj::mv(promise2));
    promises.add(kj::mv(promise3));

    // Join all promises - waits for all to complete
    auto joinedPromise = kj::joinPromises(promises.releaseAsArray())
        .then([](kj::Array<int> results) {
            int sum = 0;
            for (auto r : results) {
                sum += r;
            }
            std::cout << "  Joined sum: " << sum << std::endl;
            return sum;
        });

    int finalSum = joinedPromise.wait(ioContext.waitScope);  
    std::cout << "  Final sum from joined promises: " << finalSum << std::endl;

    std::cout << std::endl;
}

//==============================================================================
// Test 7: Performance Comparison - Packed vs Unpacked Serialization
//
// Cap'n Proto offers packed serialization that compresses zero bytes.
// This can significantly reduce message size for sparse data.
//==============================================================================
void test_packed_serialization() {
    std::cout << "=== Test 7: Packed vs Unpacked Serialization ===" << std::endl;

    // Create a message with lots of default (zero) fields
    ::capnp::MallocMessageBuilder builder;
    auto response = builder.initRoot<SkillResponse>();
    response.setTaskId(1);
    response.setSuccess(true);
    // errorMessage left as default (empty)
    
    auto mathResp = response.initPayload().initMathOperation();
    mathResp.setResult(42.0);
    mathResp.setOverflow(false);

    // Unpacked serialization
    auto unpacked = capnp::messageToFlatArray(builder);
    auto unpackedBytes = unpacked.asBytes();

    // Packed serialization - compress runs of zeros
    kj::VectorOutputStream output;
    capnp::writePackedMessage(output, builder);
    auto packedBytes = output.getArray();

    std::cout << "Unpacked size: " << unpackedBytes.size() << " bytes" << std::endl;
    std::cout << "Packed size: " << packedBytes.size() << " bytes" << std::endl;
    std::cout << "Compression ratio: " 
              << (100.0 * packedBytes.size() / unpackedBytes.size()) << "%" << std::endl;

    std::cout << std::endl;
}

//==============================================================================
// Main
//==============================================================================
int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Cap'n Proto Feature Demonstration" << std::endl;
    std::cout << "========================================\n" << std::endl;

    test_basic_serialization();
    test_unions();
    test_lists();
    test_nested_structures();
    test_batch_processing();
    test_async_promise();
    test_packed_serialization();

    std::cout << "========================================" << std::endl;
    std::cout << "All tests completed successfully!" << std::endl;
    std::cout << "========================================\n" << std::endl;

    return 0;
}
