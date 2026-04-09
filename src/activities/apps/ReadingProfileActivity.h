#pragma once

#include <cstdint>

#include "../Activity.h"

class ReadingProfileActivity final : public Activity {
  int scrollOffset = 0;
  int maxScrollOffset = 0;
  uint32_t lastScrollActionMs = 0;
  int scrollDirection = 0;

 public:
  explicit ReadingProfileActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ReadingProfile", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
