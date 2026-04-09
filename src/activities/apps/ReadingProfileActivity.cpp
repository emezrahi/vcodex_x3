#include "ReadingProfileActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/HeaderDateUtils.h"
#include "util/TimeUtils.h"

namespace {
constexpr int RADAR_RADIUS = 64;
constexpr int CONTENT_TOP_GAP = 18;
constexpr int RADAR_TOP_GAP = 34;
constexpr int RADAR_SECTION_HEIGHT = 252;
constexpr int SCORE_TOP_GAP = 20;
constexpr int SCORE_CARD_HEIGHT = 74;
constexpr int SECTION_TITLE_HEIGHT = 20;
constexpr int SECTION_DESCRIPTION_TOP_GAP = 8;
constexpr int SECTION_DESCRIPTION_BOTTOM_GAP = 12;
constexpr int SECTION_CARD_HEIGHT = 74;
constexpr int SECTION_TERTIARY_CARD_HEIGHT = 100;
constexpr int SECTION_CARD_GAP = 16;
constexpr int SECTION_ROW_GAP = 20;
constexpr int SECTION_EXTRA_CARD_ROW_GAP = 12;
constexpr int CONTENT_SCROLL_STEP = 110;
constexpr uint32_t SCROLL_REPEAT_START_MS = 260;
constexpr uint32_t SCROLL_REPEAT_INTERVAL_MS = 130;
constexpr uint32_t LAST_7_DAYS = 7;
constexpr uint32_t TEN_MINUTES_MS = 10U * 60U * 1000U;
constexpr uint32_t THIRTY_MINUTES_MS = 30U * 60U * 1000U;

enum class AxisLabelAlign { Left, Center, Right };

struct AxisSummary {
  StrId labelId = StrId::STR_NONE_OPT;
  int score = 0;
  std::string primaryValue;
  StrId primaryLabelId = StrId::STR_NONE_OPT;
  std::string secondaryValue;
  StrId secondaryLabelId = StrId::STR_NONE_OPT;
  std::string tertiaryValue;
  StrId tertiaryLabelId = StrId::STR_NONE_OPT;
};

struct ReadingProfileSummary {
  int totalScore = 0;
  int daysRead = 0;
  int goalDays = 0;
  int longestReadStreak = 0;
  int bestDaySharePercent = 0;
  int sessions = 0;
  int sessionsPerReadDayTenths = 0;
  int sessionsUnder10mPercent = 0;
  int sessions10to29mPercent = 0;
  int sessions30mPlusPercent = 0;
  AxisSummary habit;
  AxisSummary stability;
  AxisSummary engagement;
  AxisSummary depth;
};

struct ProfileSection {
  const AxisSummary* summary = nullptr;
  StrId descriptionId = StrId::STR_NONE_OPT;
};

int roundDiv(const int numerator, const int denominator) {
  if (denominator <= 0) {
    return 0;
  }
  return (numerator + denominator / 2) / denominator;
}

int clampPercent(const int value) { return std::clamp(value, 0, 100); }

std::string formatFraction(const int value, const int total) { return std::to_string(value) + "/" + std::to_string(total); }

std::string formatPercentLabel(const int value) { return std::to_string(clampPercent(value)) + "%"; }

std::string formatTenths(const int tenths) {
  const int whole = tenths / 10;
  const int fraction = std::abs(tenths % 10);
  if (fraction == 0) {
    return std::to_string(whole);
  }
  return std::to_string(whole) + "." + std::to_string(fraction);
}

uint32_t getDisplayReferenceDayOrdinal() {
  const uint32_t displayTimestamp = READING_STATS.getDisplayTimestamp();
  if (TimeUtils::isClockValid(displayTimestamp)) {
    return TimeUtils::getLocalDayOrdinal(displayTimestamp);
  }

  uint32_t latestDayOrdinal = 0;
  const auto& readingDays = READING_STATS.getReadingDays();
  if (!readingDays.empty()) {
    latestDayOrdinal = std::max(latestDayOrdinal, readingDays.back().dayOrdinal);
  }

  const auto& sessionLog = READING_STATS.getSessionLog();
  if (!sessionLog.empty()) {
    latestDayOrdinal = std::max(latestDayOrdinal, sessionLog.back().dayOrdinal);
  }

  return latestDayOrdinal;
}

bool shouldDrawDitheredPixel(const int x, const int y, const Color color) {
  switch (color) {
    case Color::Black:
      return true;
    case Color::DarkGray:
      return (x + y) % 2 == 0;
    case Color::LightGray:
      return x % 2 == 0 && y % 2 == 0;
    case Color::White:
    case Color::Clear:
    default:
      return false;
  }
}

void fillPolygonDither(GfxRenderer& renderer, const int* xPoints, const int* yPoints, const int numPoints, const Color color) {
  if (numPoints < 3) {
    return;
  }

  int minY = yPoints[0];
  int maxY = yPoints[0];
  for (int index = 1; index < numPoints; ++index) {
    minY = std::min(minY, yPoints[index]);
    maxY = std::max(maxY, yPoints[index]);
  }

  minY = std::max(0, minY);
  maxY = std::min(renderer.getScreenHeight() - 1, maxY);
  std::vector<int> nodeX(static_cast<size_t>(numPoints));

  for (int scanY = minY; scanY <= maxY; ++scanY) {
    int nodes = 0;
    int previous = numPoints - 1;
    for (int index = 0; index < numPoints; ++index) {
      if ((yPoints[index] < scanY && yPoints[previous] >= scanY) ||
          (yPoints[previous] < scanY && yPoints[index] >= scanY)) {
        const int deltaY = yPoints[previous] - yPoints[index];
        if (deltaY != 0) {
          nodeX[static_cast<size_t>(nodes++)] =
              xPoints[index] + (scanY - yPoints[index]) * (xPoints[previous] - xPoints[index]) / deltaY;
        }
      }
      previous = index;
    }

    std::sort(nodeX.begin(), nodeX.begin() + nodes);
    for (int index = 0; index < nodes - 1; index += 2) {
      const int startX = std::max(0, nodeX[static_cast<size_t>(index)]);
      const int endX = std::min(renderer.getScreenWidth() - 1, nodeX[static_cast<size_t>(index + 1)]);
      for (int x = startX; x <= endX; ++x) {
        if (shouldDrawDitheredPixel(x, scanY, color)) {
          renderer.drawPixel(x, scanY, true);
        }
      }
    }
  }
}

void drawCompactMetricCard(GfxRenderer& renderer, const Rect& rect, const std::string& value, const char* label) {
  renderer.fillRectDither(rect.x, rect.y, rect.width, rect.height, Color::LightGray);
  renderer.drawRect(rect.x, rect.y, rect.width, rect.height);

  const int valueFontId =
      renderer.getTextWidth(UI_12_FONT_ID, value.c_str(), EpdFontFamily::BOLD) <= rect.width - 20 ? UI_12_FONT_ID
                                                                                                    : UI_10_FONT_ID;
  renderer.drawText(valueFontId, rect.x + 10, rect.y + 10, value.c_str(), true, EpdFontFamily::BOLD);

  const auto labelLines = renderer.wrappedText(UI_10_FONT_ID, label, rect.width - 20, 3, EpdFontFamily::REGULAR);
  int labelY = rect.y + 10 + renderer.getLineHeight(valueFontId) + 6;
  for (const auto& line : labelLines) {
    renderer.drawText(UI_10_FONT_ID, rect.x + 10, labelY, line.c_str());
    labelY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

int getSectionCardHeight(const AxisSummary& axis) {
  return axis.tertiaryLabelId != StrId::STR_NONE_OPT ? SECTION_TERTIARY_CARD_HEIGHT : SECTION_CARD_HEIGHT;
}

void drawDiamond(GfxRenderer& renderer, const int centerX, const int centerY, const int radius, const bool state) {
  renderer.drawLine(centerX, centerY - radius, centerX + radius, centerY, state);
  renderer.drawLine(centerX + radius, centerY, centerX, centerY + radius, state);
  renderer.drawLine(centerX, centerY + radius, centerX - radius, centerY, state);
  renderer.drawLine(centerX - radius, centerY, centerX, centerY - radius, state);
}

void drawAlignedTextLine(GfxRenderer& renderer, const int fontId, const int x, const int y, const int width,
                         const char* text, const AxisLabelAlign align, const bool black = true,
                         const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int textWidth = renderer.getTextWidth(fontId, text, style);
  int drawX = x;
  switch (align) {
    case AxisLabelAlign::Center:
      drawX = x + std::max(0, (width - textWidth) / 2);
      break;
    case AxisLabelAlign::Right:
      drawX = x + std::max(0, width - textWidth);
      break;
    case AxisLabelAlign::Left:
    default:
      drawX = x;
      break;
  }
  renderer.drawText(fontId, drawX, y, text, black, style);
}

void drawAxisSummary(GfxRenderer& renderer, const int x, const int y, const int width, const int score, const char* label,
                     const AxisLabelAlign align) {
  const std::string scoreText = std::to_string(score);
  drawAlignedTextLine(renderer, UI_12_FONT_ID, x, y, width, scoreText.c_str(), align, true, EpdFontFamily::BOLD);

  const auto labelLines = renderer.wrappedText(UI_10_FONT_ID, label, width, 2, EpdFontFamily::REGULAR);
  int labelY = y + renderer.getLineHeight(UI_12_FONT_ID) + 4;
  for (const auto& line : labelLines) {
    drawAlignedTextLine(renderer, UI_10_FONT_ID, x, labelY, width, line.c_str(), align);
    labelY += renderer.getLineHeight(UI_10_FONT_ID);
  }
}

StrId getSectionDescriptionId(const StrId labelId) {
  switch (labelId) {
    case StrId::STR_HABIT:
      return StrId::STR_READING_PROFILE_HABIT_DESC;
    case StrId::STR_STABILITY:
      return StrId::STR_READING_PROFILE_STABILITY_DESC;
    case StrId::STR_ENGAGEMENT:
      return StrId::STR_READING_PROFILE_ENGAGEMENT_DESC;
    case StrId::STR_DEPTH:
      return StrId::STR_READING_PROFILE_DEPTH_DESC;
    default:
      return StrId::STR_NONE_OPT;
  }
}

std::vector<std::string> getSectionDescriptionLines(GfxRenderer& renderer, const int maxWidth, const StrId labelId) {
  const StrId descriptionId = getSectionDescriptionId(labelId);
  if (descriptionId == StrId::STR_NONE_OPT) {
    return {};
  }
  return renderer.wrappedText(UI_10_FONT_ID, I18N.get(descriptionId), maxWidth, 4, EpdFontFamily::REGULAR);
}

std::array<ProfileSection, 4> getProfileSections(const ReadingProfileSummary& summary) {
  return {ProfileSection{&summary.habit, getSectionDescriptionId(summary.habit.labelId)},
          ProfileSection{&summary.stability, getSectionDescriptionId(summary.stability.labelId)},
          ProfileSection{&summary.engagement, getSectionDescriptionId(summary.engagement.labelId)},
          ProfileSection{&summary.depth, getSectionDescriptionId(summary.depth.labelId)}};
}

int getContentBottom(GfxRenderer& renderer, const int contentTop, const int textWidth, const ReadingProfileSummary& summary) {
  int sectionTop = contentTop + RADAR_TOP_GAP + RADAR_SECTION_HEIGHT + SCORE_TOP_GAP + SCORE_CARD_HEIGHT + SECTION_ROW_GAP;
  for (const auto& section : getProfileSections(summary)) {
    const auto descriptionLines = getSectionDescriptionLines(renderer, textWidth, section.summary->labelId);
    const int cardHeight = getSectionCardHeight(*section.summary);
    sectionTop += SECTION_TITLE_HEIGHT + SECTION_DESCRIPTION_TOP_GAP +
                  static_cast<int>(descriptionLines.size()) * renderer.getLineHeight(UI_10_FONT_ID) +
                  SECTION_DESCRIPTION_BOTTOM_GAP + cardHeight +
                  (section.summary->tertiaryLabelId != StrId::STR_NONE_OPT ? SECTION_EXTRA_CARD_ROW_GAP + cardHeight
                                                                          : 0) +
                  SECTION_ROW_GAP;
  }
  return sectionTop;
}

ReadingProfileSummary buildReadingProfileSummary() {
  ReadingProfileSummary summary;
  summary.habit.labelId = StrId::STR_HABIT;
  summary.stability.labelId = StrId::STR_STABILITY;
  summary.engagement.labelId = StrId::STR_ENGAGEMENT;
  summary.depth.labelId = StrId::STR_DEPTH;

  const uint32_t referenceDayOrdinal = getDisplayReferenceDayOrdinal();
  if (referenceDayOrdinal == 0) {
    summary.habit.primaryValue = formatFraction(0, LAST_7_DAYS);
    summary.habit.primaryLabelId = StrId::STR_DAYS_READ;
    summary.habit.secondaryValue = formatFraction(0, LAST_7_DAYS);
    summary.habit.secondaryLabelId = StrId::STR_GOALS_MET;
    summary.stability.primaryValue = "0d";
    summary.stability.primaryLabelId = StrId::STR_READ_STREAK;
    summary.stability.secondaryValue = "0%";
    summary.stability.secondaryLabelId = StrId::STR_BEST_DAY_SHARE;
    summary.engagement.primaryValue = "0";
    summary.engagement.primaryLabelId = StrId::STR_SESSIONS;
    summary.engagement.secondaryValue = "0";
    summary.engagement.secondaryLabelId = StrId::STR_PER_READ_DAY;
    summary.depth.primaryValue = "0%";
    summary.depth.primaryLabelId = StrId::STR_SESSIONS_UNDER_10M;
    summary.depth.secondaryValue = "0%";
    summary.depth.secondaryLabelId = StrId::STR_SESSIONS_10M_TO_29M;
    summary.depth.tertiaryValue = "0%";
    summary.depth.tertiaryLabelId = StrId::STR_SESSIONS_30M_PLUS;
    return summary;
  }

  const uint32_t startDayOrdinal = referenceDayOrdinal >= (LAST_7_DAYS - 1) ? referenceDayOrdinal - (LAST_7_DAYS - 1) : 0;
  const uint64_t dailyGoalMs = getDailyReadingGoalMs();
  std::array<uint64_t, LAST_7_DAYS> readingMsByDay = {};
  for (const auto& day : READING_STATS.getReadingDays()) {
    if (day.dayOrdinal < startDayOrdinal || day.dayOrdinal > referenceDayOrdinal) {
      continue;
    }
    const size_t index = static_cast<size_t>(day.dayOrdinal - startDayOrdinal);
    if (index < readingMsByDay.size()) {
      readingMsByDay[index] += day.readingMs;
    }
  }

  uint64_t weeklyTotalReadingMs = 0;
  uint64_t maxDayReadingMs = 0;
  int currentStreak = 0;
  for (const uint64_t readingMs : readingMsByDay) {
    weeklyTotalReadingMs += readingMs;
    maxDayReadingMs = std::max(maxDayReadingMs, readingMs);
    if (readingMs > 0) {
      summary.daysRead++;
      currentStreak++;
      summary.longestReadStreak = std::max(summary.longestReadStreak, currentStreak);
      if (readingMs >= dailyGoalMs) {
        summary.goalDays++;
      }
    } else {
      currentStreak = 0;
    }
  }

  if (weeklyTotalReadingMs > 0) {
    summary.bestDaySharePercent = roundDiv(static_cast<int>(maxDayReadingMs * 100ULL), static_cast<int>(weeklyTotalReadingMs));
  }

  std::vector<ReadingSessionLogEntry> recentSessions;
  recentSessions.reserve(16);
  for (const auto& session : READING_STATS.getSessionLog()) {
    if (session.dayOrdinal >= startDayOrdinal && session.dayOrdinal <= referenceDayOrdinal) {
      recentSessions.push_back(session);
    }
  }

  if (recentSessions.empty() && summary.daysRead > 0) {
    for (const uint64_t readingMs : readingMsByDay) {
      if (readingMs == 0) {
        continue;
      }
      recentSessions.push_back(ReadingSessionLogEntry{
          0, static_cast<uint32_t>(std::min<uint64_t>(readingMs, static_cast<uint64_t>(UINT32_MAX)))});
    }
  }

  int sessionsUnder10m = 0;
  int sessions10to29m = 0;
  int sessions30m = 0;
  for (const auto& session : recentSessions) {
    summary.sessions++;
    if (session.sessionMs < TEN_MINUTES_MS) {
      sessionsUnder10m++;
    } else if (session.sessionMs < THIRTY_MINUTES_MS) {
      sessions10to29m++;
    } else {
      sessions30m++;
    }
  }

  if (summary.daysRead > 0) {
    summary.sessionsPerReadDayTenths = roundDiv(summary.sessions * 10, summary.daysRead);
  }
  if (summary.sessions > 0) {
    summary.sessionsUnder10mPercent = roundDiv(sessionsUnder10m * 100, summary.sessions);
    summary.sessions10to29mPercent = roundDiv(sessions10to29m * 100, summary.sessions);
    if (summary.sessionsUnder10mPercent + summary.sessions10to29mPercent > 100) {
      if (summary.sessions10to29mPercent >= summary.sessionsUnder10mPercent) {
        summary.sessions10to29mPercent = 100 - summary.sessionsUnder10mPercent;
      } else {
        summary.sessionsUnder10mPercent = 100 - summary.sessions10to29mPercent;
      }
    }
    summary.sessions30mPlusPercent =
        clampPercent(100 - summary.sessionsUnder10mPercent - summary.sessions10to29mPercent);
  }

  const int habitScore = clampPercent(roundDiv(summary.daysRead * 65 + summary.goalDays * 35, static_cast<int>(LAST_7_DAYS)));
  const int streakScore = summary.daysRead > 0 ? roundDiv(summary.longestReadStreak * 100, summary.daysRead) : 0;
  int balanceScore = 0;
  if (summary.daysRead > 1 && weeklyTotalReadingMs > 0) {
    const double bestShare = static_cast<double>(maxDayReadingMs) / static_cast<double>(weeklyTotalReadingMs);
    const double idealShare = 1.0 / static_cast<double>(summary.daysRead);
    const double normalized = 1.0 - ((bestShare - idealShare) / (1.0 - idealShare));
    balanceScore = clampPercent(static_cast<int>(normalized * 100.0 + 0.5));
  }
  const int stabilityScore = clampPercent((streakScore + balanceScore + 1) / 2);
  const int sessionsScore = std::min(100, summary.sessions * 10);
  const int sessionsPerReadDayScore =
      summary.daysRead > 0 ? std::min(100, roundDiv(summary.sessions * 100, summary.daysRead * 3)) : 0;
  const int engagementScore = clampPercent((sessionsScore * 60 + sessionsPerReadDayScore * 40 + 50) / 100);
  const int depthScore = clampPercent(
      roundDiv(summary.sessions10to29mPercent * 50 + summary.sessions30mPlusPercent * 100, 100));

  summary.totalScore = clampPercent((habitScore + stabilityScore + engagementScore + depthScore + 2) / 4);

  summary.habit.score = habitScore;
  summary.habit.primaryValue = formatFraction(summary.daysRead, LAST_7_DAYS);
  summary.habit.primaryLabelId = StrId::STR_DAYS_READ;
  summary.habit.secondaryValue = formatFraction(summary.goalDays, LAST_7_DAYS);
  summary.habit.secondaryLabelId = StrId::STR_GOALS_MET;

  summary.stability.score = stabilityScore;
  summary.stability.primaryValue = std::to_string(summary.longestReadStreak) + "d";
  summary.stability.primaryLabelId = StrId::STR_READ_STREAK;
  summary.stability.secondaryValue = formatPercentLabel(summary.bestDaySharePercent);
  summary.stability.secondaryLabelId = StrId::STR_BEST_DAY_SHARE;

  summary.engagement.score = engagementScore;
  summary.engagement.primaryValue = std::to_string(summary.sessions);
  summary.engagement.primaryLabelId = StrId::STR_SESSIONS;
  summary.engagement.secondaryValue = formatTenths(summary.sessionsPerReadDayTenths);
  summary.engagement.secondaryLabelId = StrId::STR_PER_READ_DAY;

  summary.depth.score = depthScore;
  summary.depth.primaryValue = formatPercentLabel(summary.sessionsUnder10mPercent);
  summary.depth.primaryLabelId = StrId::STR_SESSIONS_UNDER_10M;
  summary.depth.secondaryValue = formatPercentLabel(summary.sessions10to29mPercent);
  summary.depth.secondaryLabelId = StrId::STR_SESSIONS_10M_TO_29M;
  summary.depth.tertiaryValue = formatPercentLabel(summary.sessions30mPlusPercent);
  summary.depth.tertiaryLabelId = StrId::STR_SESSIONS_30M_PLUS;

  return summary;
}
}  // namespace

void ReadingProfileActivity::onEnter() {
  Activity::onEnter();
  scrollOffset = 0;
  maxScrollOffset = 0;
  lastScrollActionMs = 0;
  scrollDirection = 0;
  requestUpdate();
}

void ReadingProfileActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto scrollBy = [&](const int delta) {
    const int nextOffset = std::clamp(scrollOffset + delta, 0, maxScrollOffset);
    if (nextOffset != scrollOffset) {
      scrollOffset = nextOffset;
      requestUpdate();
    }
  };

  int requestedDirection = 0;
  if (mappedInput.isPressed(MappedInputManager::Button::Up) || mappedInput.isPressed(MappedInputManager::Button::Left)) {
    requestedDirection = -1;
  } else if (mappedInput.isPressed(MappedInputManager::Button::Down) ||
             mappedInput.isPressed(MappedInputManager::Button::Right)) {
    requestedDirection = 1;
  }

  if (requestedDirection == 0) {
    scrollDirection = 0;
    lastScrollActionMs = 0;
    return;
  }

  const bool directionChanged = requestedDirection != scrollDirection;
  const bool wasPressedNow =
      (requestedDirection < 0 &&
       (mappedInput.wasPressed(MappedInputManager::Button::Up) || mappedInput.wasPressed(MappedInputManager::Button::Left))) ||
      (requestedDirection > 0 &&
       (mappedInput.wasPressed(MappedInputManager::Button::Down) || mappedInput.wasPressed(MappedInputManager::Button::Right)));
  const bool canRepeat = mappedInput.getHeldTime() >= SCROLL_REPEAT_START_MS &&
                         (lastScrollActionMs == 0 || millis() - lastScrollActionMs >= SCROLL_REPEAT_INTERVAL_MS);

  if (directionChanged || wasPressedNow || canRepeat) {
    scrollBy(requestedDirection * CONTENT_SCROLL_STEP);
    lastScrollActionMs = millis();
  }

  scrollDirection = requestedDirection;
}

void ReadingProfileActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto summary = buildReadingProfileSummary();
  const auto sections = getProfileSections(summary);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int sidePadding = metrics.contentSidePadding;
  const int textWidth = pageWidth - sidePadding * 2;
  const int sectionWidth = (pageWidth - sidePadding * 2 - SECTION_CARD_GAP) / 2;
  const std::string title = std::string(tr(STR_READING_PROFILE)) + " - " + tr(STR_LAST_7D);
  const int contentTop = metrics.topPadding + metrics.headerHeight + CONTENT_TOP_GAP;
  const int viewportBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  maxScrollOffset = std::max(0, getContentBottom(renderer, contentTop, textWidth, summary) - viewportBottom);
  scrollOffset = std::clamp(scrollOffset, 0, maxScrollOffset);

  const int radarTop = contentTop + RADAR_TOP_GAP - scrollOffset;
  const int radarCenterX = pageWidth / 2;
  const int radarCenterY = radarTop + 108;
  const int radarXs[4] = {radarCenterX, radarCenterX + RADAR_RADIUS, radarCenterX, radarCenterX - RADAR_RADIUS};
  const int radarYs[4] = {radarCenterY - RADAR_RADIUS, radarCenterY, radarCenterY + RADAR_RADIUS, radarCenterY};

  fillPolygonDither(renderer, radarXs, radarYs, 4, Color::LightGray);
  for (int radiusPercent : {25, 50, 75, 100}) {
    const int guideRadius = RADAR_RADIUS * radiusPercent / 100;
    drawDiamond(renderer, radarCenterX, radarCenterY, guideRadius, true);
  }
  renderer.drawLine(radarCenterX, radarCenterY - RADAR_RADIUS, radarCenterX, radarCenterY + RADAR_RADIUS);
  renderer.drawLine(radarCenterX - RADAR_RADIUS, radarCenterY, radarCenterX + RADAR_RADIUS, radarCenterY);

  const int scoreXs[4] = {radarCenterX,
                          radarCenterX + RADAR_RADIUS * summary.engagement.score / 100,
                          radarCenterX,
                          radarCenterX - RADAR_RADIUS * summary.depth.score / 100};
  const int scoreYs[4] = {radarCenterY - RADAR_RADIUS * summary.habit.score / 100,
                          radarCenterY,
                          radarCenterY + RADAR_RADIUS * summary.stability.score / 100,
                          radarCenterY};
  fillPolygonDither(renderer, scoreXs, scoreYs, 4, Color::DarkGray);
  drawDiamond(renderer, radarCenterX, radarCenterY, RADAR_RADIUS, true);
  for (int index = 0; index < 4; ++index) {
    const int next = (index + 1) % 4;
    renderer.drawLine(scoreXs[index], scoreYs[index], scoreXs[next], scoreYs[next], 2, true);
  }

  drawAxisSummary(renderer, radarCenterX - 56, radarTop - 26, 112, summary.habit.score, tr(STR_HABIT),
                  AxisLabelAlign::Center);
  drawAxisSummary(renderer, radarCenterX - 72, radarCenterY + RADAR_RADIUS + 4, 144, summary.stability.score,
                  tr(STR_STABILITY), AxisLabelAlign::Center);
  drawAxisSummary(renderer, radarCenterX + RADAR_RADIUS + 28, radarCenterY - 18, 126, summary.engagement.score,
                  tr(STR_ENGAGEMENT), AxisLabelAlign::Left);
  drawAxisSummary(renderer, radarCenterX - RADAR_RADIUS - 154, radarCenterY - 18, 126, summary.depth.score,
                  tr(STR_DEPTH), AxisLabelAlign::Right);

  const int scoreTop = radarTop + RADAR_SECTION_HEIGHT + SCORE_TOP_GAP;
  const Rect scoreRect{sidePadding, scoreTop, pageWidth - sidePadding * 2, SCORE_CARD_HEIGHT};
  renderer.fillRectDither(scoreRect.x, scoreRect.y, scoreRect.width, scoreRect.height, Color::LightGray);
  renderer.drawRect(scoreRect.x, scoreRect.y, scoreRect.width, scoreRect.height);
  const std::string totalScoreLabel = std::to_string(summary.totalScore);
  const int totalScoreWidth = renderer.getTextWidth(UI_12_FONT_ID, totalScoreLabel.c_str(), EpdFontFamily::BOLD);
  const int scoreValueY = scoreRect.y + 10;
  const int scoreLabelY = scoreValueY + renderer.getLineHeight(UI_12_FONT_ID) + 8;
  renderer.drawText(UI_12_FONT_ID, scoreRect.x + (scoreRect.width - totalScoreWidth) / 2, scoreValueY,
                    totalScoreLabel.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, scoreLabelY, tr(STR_SCORE));

  int sectionTop = scoreTop + SCORE_CARD_HEIGHT + SECTION_ROW_GAP;
  for (const auto& section : sections) {
    const AxisSummary& axis = *section.summary;
    const int cardHeight = getSectionCardHeight(axis);
    renderer.drawText(UI_10_FONT_ID, sidePadding, sectionTop, I18N.get(axis.labelId), true, EpdFontFamily::BOLD);

    const int descriptionTop = sectionTop + SECTION_TITLE_HEIGHT + SECTION_DESCRIPTION_TOP_GAP;
    const auto descriptionLines = getSectionDescriptionLines(renderer, textWidth, axis.labelId);
    int descriptionY = descriptionTop;
    for (const auto& line : descriptionLines) {
      renderer.drawText(UI_10_FONT_ID, sidePadding, descriptionY, line.c_str());
      descriptionY += renderer.getLineHeight(UI_10_FONT_ID);
    }

    const int cardsTop = descriptionTop +
                         static_cast<int>(descriptionLines.size()) * renderer.getLineHeight(UI_10_FONT_ID) +
                         SECTION_DESCRIPTION_BOTTOM_GAP;
    if (axis.tertiaryLabelId != StrId::STR_NONE_OPT) {
      drawCompactMetricCard(renderer, Rect{sidePadding, cardsTop, sectionWidth, cardHeight}, axis.primaryValue,
                            I18N.get(axis.primaryLabelId));
      drawCompactMetricCard(renderer,
                            Rect{sidePadding + sectionWidth + SECTION_CARD_GAP, cardsTop, sectionWidth, cardHeight},
                            axis.secondaryValue, I18N.get(axis.secondaryLabelId));
      drawCompactMetricCard(renderer,
                            Rect{sidePadding, cardsTop + cardHeight + SECTION_EXTRA_CARD_ROW_GAP,
                                 pageWidth - sidePadding * 2, cardHeight},
                            axis.tertiaryValue, I18N.get(axis.tertiaryLabelId));
    } else {
      drawCompactMetricCard(renderer, Rect{sidePadding, cardsTop, sectionWidth, cardHeight}, axis.primaryValue,
                            I18N.get(axis.primaryLabelId));
      drawCompactMetricCard(renderer,
                            Rect{sidePadding + sectionWidth + SECTION_CARD_GAP, cardsTop, sectionWidth, cardHeight},
                            axis.secondaryValue, I18N.get(axis.secondaryLabelId));
    }
    sectionTop = cardsTop + cardHeight +
                 (axis.tertiaryLabelId != StrId::STR_NONE_OPT ? SECTION_EXTRA_CARD_ROW_GAP + cardHeight : 0) +
                 SECTION_ROW_GAP;
  }

  renderer.fillRect(0, 0, pageWidth, contentTop, false);
  if (viewportBottom < renderer.getScreenHeight()) {
    renderer.fillRect(0, viewportBottom, pageWidth, renderer.getScreenHeight() - viewportBottom, false);
  }

  HeaderDateUtils::drawHeaderWithDate(renderer, title.c_str());

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", scrollOffset > 0 ? tr(STR_DIR_UP) : "",
                                            scrollOffset < maxScrollOffset ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
