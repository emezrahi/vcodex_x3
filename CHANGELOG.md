# Changelog

Brief firmware history for `crosspoint-vcodex`.

## 1.1.3-vcodex

- made `Daily Goal` configurable in `Settings > Apps` with `15 min / 30 min / 45 min / 60 min`
- aligned `Goal Streak`, Home stats shortcut, Reading Stats cards and Heatmap logic with the selected goal
- updated achievement goal-day and goal-streak progress so they stay coherent after changing the configured goal

Version code: `2026032903`

## 1.1.2-vcodex

- formalized firmware versioning and version code tracking
- added a concise changelog to make release-to-release changes easier to follow
- kept the current feature set stable as the new documented release line

Version code: `2026032902`

## 1.1.1-vcodex

- added `Achievements` with retroactive unlock bootstrap, popups and reset controls
- added `Sync with prev. stats` for achievements
- added shortcut visibility management and separate Home/Apps ordering
- added `/ignore_stats/` exclusion for stats, sessions, timeline, heatmap and achievement-related tracking
- polished Home/Apps navigation and fixed several app-entry double-open issues

Version code: `2026032901`

## 1.1.0-vcodex

- added `Reading Heatmap`, `Reading Day` and `Reading Timeline`
- expanded `Reading Stats` with richer per-book and aggregate views
- added `Sync Day` diagnostics, configurable date format and time zone selection
- improved Home with dynamic reading stats shortcut and configurable shortcut placement
- added a global `Bookmarks` app for EPUB bookmarks

## 1.0.0-vcodex

- established the fork identity and `Lyra Custom` default theme
- added the first `Sync Day + fallback` model for day-coherent reading analytics
- added the first wave of reading-focused UX improvements, custom sleep handling and stats workflows
- kept full CrossPoint Reader compatibility as the base firmware
