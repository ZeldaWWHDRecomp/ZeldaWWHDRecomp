#pragma once
#ifdef __ANDROID__
#include "loader.h"
#include <memory>
#include <optional>
struct ANativeWindow;
namespace gfxvk {
// One secondary logical device/queue and one outstanding worker request.
// Main-thread methods only record a bounded snapshot or poll the mailbox.
class AndroidIsolatedPresenter {
public:
 enum class Operation { Surface, Acquire, Present };
 struct Result { Operation operation; VkResult status; VkExtent2D extent{}; uint64_t elapsedNs=0; };
 AndroidIsolatedPresenter(VkInstance,VkPhysicalDevice,VkDevice primary,uint32_t family,bool dynamicRenderingKHR);
 ~AndroidIsolatedPresenter();
 AndroidIsolatedPresenter(const AndroidIsolatedPresenter&)=delete;
 AndroidIsolatedPresenter& operator=(const AndroidIsolatedPresenter&)=delete;
 VkQueue queue() const;
 bool busy() const;
 bool acquired() const;
 bool prepared() const;
 bool published() const;
 // Takes ownership of exactly one native-window reference, including failures.
 void replace_surface(ANativeWindow*,uint32_t width,uint32_t height,int presentMode);
 void resize(int presentMode);
 void acquire();
 std::optional<Result> poll();
 Result wait(); // explicit shutdown/drain only
 // Caller transitions the source to TRANSFER_SRC. The returned semaphores must
 // be included in the primary submission containing these commands.
 void record_snapshot(VkCommandBuffer,VkImage source,VkFormat sourceFormat,VkExtent2D);
 VkSemaphore snapshot_wait() const;
 VkSemaphore snapshot_signal() const;
 void primary_submitted(); // only after the signal has been queued
 void present(bool sourceLinear,int filter,bool fxaa);
private:
 struct Impl;
 std::unique_ptr<Impl> impl;
};
}
#endif
