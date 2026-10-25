// Exercise the actual entry points, including the driver's blocking interval.
#include "../src/vulkan_layer/vulkan_queue_layer.cpp"

#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
struct FakeQueue { void* dispatch; };
std::mutex test_mutex;
std::condition_variable test_condition;
bool driver_entered = false;
bool driver_released = false;

VKAPI_ATTR VkResult VKAPI_CALL blocked_wait(VkQueue) {
    std::unique_lock lock(test_mutex);
    driver_entered = true;
    test_condition.notify_all();
    test_condition.wait(lock, [] { return driver_released; });
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL submit_ok(VkQueue, uint32_t, const VkSubmitInfo*, VkFence) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL submit2_ok(VkQueue, uint32_t, const VkSubmitInfo2*, VkFence) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL sparse_ok(VkQueue, uint32_t, const VkBindSparseInfo*, VkFence) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL present_ok(VkQueue, const VkPresentInfoKHR*) { return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL wait_ok(VkQueue) { return VK_SUCCESS; }

enum class Operation { submit, submit2, submit2_khr, sparse, present, idle };
VkResult invoke(Operation op, VkQueue queue) {
    switch (op) {
    case Operation::submit: return layer_queue_submit(queue, 0, nullptr, VK_NULL_HANDLE);
    case Operation::submit2: return layer_queue_submit2(queue, 0, nullptr, VK_NULL_HANDLE);
    case Operation::submit2_khr: return layer_queue_submit2_khr(queue, 0, nullptr, VK_NULL_HANDLE);
    case Operation::sparse: return layer_queue_bind_sparse(queue, 0, nullptr, VK_NULL_HANDLE);
    case Operation::present: return layer_queue_present(queue, nullptr);
    case Operation::idle: return layer_queue_wait_idle(queue);
    }
    return VK_ERROR_UNKNOWN;
}

void independence_and_exclusion(Operation operation, bool same_queue) {
    int dispatch_identity{};
    FakeQueue first{&dispatch_identity}, second{&dispatch_identity};
    const auto queue_a = reinterpret_cast<VkQueue>(&first);
    const auto queue_b = reinterpret_cast<VkQueue>(same_queue ? &first : &second);
    auto table = std::make_shared<DeviceTable>();
    table->queue_submit = submit_ok;
    table->queue_submit2 = submit2_ok;
    table->queue_submit2_khr = submit2_ok;
    table->queue_bind_sparse = sparse_ok;
    table->queue_present = present_ok;
    table->queue_wait_idle = blocked_wait;
    {
        std::scoped_lock lock(test_mutex);
        driver_entered = driver_released = false;
    }
    { std::scoped_lock lock(g_tables_mutex); g_devices[&dispatch_identity] = table; }
    auto blocker = std::async(std::launch::async, [&] { return layer_queue_wait_idle(queue_a); });
    {
        std::unique_lock lock(test_mutex);
        if (!test_condition.wait_for(lock, std::chrono::seconds(2), [] { return driver_entered; }))
            throw std::runtime_error("test driver did not enter WaitIdle");
    }
    // For the idle variant the second invocation should only test queue access.
    if (operation == Operation::idle) table->queue_wait_idle = wait_ok;
    std::promise<void> attempted;
    auto attempt = attempted.get_future();
    auto other = std::async(std::launch::async, [&] {
        attempted.set_value();
        return invoke(operation, queue_b);
    });
    attempt.wait();
    const bool completed_while_blocked = other.wait_for(std::chrono::milliseconds(same_queue ? 50 : 500)) == std::future_status::ready;
    {
        std::scoped_lock lock(test_mutex);
        driver_released = true;
    }
    test_condition.notify_all();
    const auto first_result = blocker.get();
    const auto second_result = other.get();
    { std::scoped_lock lock(g_tables_mutex); g_devices.erase(&dispatch_identity); }
    if (first_result != VK_SUCCESS || second_result != VK_SUCCESS)
        throw std::runtime_error("queue result was changed");
    if (completed_while_blocked == same_queue)
        throw std::runtime_error(same_queue ? "same queue entered driver concurrently" : "independent queue blocked behind another queue's WaitIdle");
}
}

int main() {
    try {
        for (auto op : {Operation::submit, Operation::submit2, Operation::submit2_khr,
                        Operation::sparse, Operation::present, Operation::idle}) {
            independence_and_exclusion(op, false);
            independence_and_exclusion(op, true);
        }
        std::cout << "Six actual queue entry points: independent queues progress during WaitIdle; same queue remains serialized; native results preserved.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
