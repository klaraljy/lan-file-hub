#include "lan_file_hub/application/app_context.h"

#include <mutex>
#include <utility>

namespace lan_file_hub {
namespace {

std::mutex g_context_mutex;
std::shared_ptr<AppContext> g_context;

}  // namespace

void SetAppContext(std::shared_ptr<AppContext> context) {
  std::lock_guard<std::mutex> lock(g_context_mutex);
  g_context = std::move(context);
}

std::shared_ptr<AppContext> GetAppContext() {
  std::lock_guard<std::mutex> lock(g_context_mutex);
  return g_context;
}

std::string RedactWithContext(std::string text) {
  const auto context = GetAppContext();
  if (!context) {
    return text;
  }
  return RedactSecrets(std::move(text), context->secrets);
}

}  // namespace lan_file_hub
