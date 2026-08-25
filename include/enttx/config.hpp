#pragma once
#include <entt/config/config.h>
#include <entt/config/version.h>

#define ENTTX_STR(arg) #arg
#define ENTTX_XSTR(arg) ENTTX_STR(arg)

#define ENTTX_VERSION_MAJOR 1
#define ENTTX_VERSION_MINOR 2
#define ENTTX_VERSION_PATCH 0

#define ENTTX_VERSION                                                          \
  ENTTX_XSTR(ENTTX_VERSION_MAJOR)                                              \
  "." ENTTX_XSTR(ENTTX_VERSION_MINOR) "." ENTTX_XSTR(ENTTX_VERSION_PATCH)

#define ENTTX_MIN_ENTT_VERSION_MAJOR 4
#define ENTTX_MIN_ENTT_VERSION_MINOR 0
#define ENTTX_MIN_ENTT_VERSION_PATCH 0

#define ENTTX_MIN_ENTT_VERSION                                                 \
  ENTTX_XSTR(ENTTX_MIN_ENTT_VERSION_MAJOR)                                     \
  "." ENTTX_XSTR(ENTTX_MIN_ENTT_VERSION_MINOR) "." ENTTX_XSTR(                 \
      ENTTX_MIN_ENTT_VERSION_PATCH)

static_assert(ENTT_VERSION_MAJOR > ENTTX_MIN_ENTT_VERSION_MAJOR ||
                  (ENTT_VERSION_MAJOR == ENTTX_MIN_ENTT_VERSION_MAJOR &&
                   ENTT_VERSION_MINOR >= ENTTX_MIN_ENTT_VERSION_MINOR) ||
                  (ENTT_VERSION_MAJOR == ENTTX_MIN_ENTT_VERSION_MAJOR &&
                   ENTT_VERSION_MINOR == ENTTX_MIN_ENTT_VERSION_MINOR &&
                   ENTT_VERSION_PATCH >= ENTTX_MIN_ENTT_VERSION_PATCH),
              "enttx requires EnTT version >= " ENTTX_MIN_ENTT_VERSION);

#ifdef ENTTX_DISABLE_ASSERT
#undef ENTTX_ASSERT
#undef ENTTX_USER_ASSERT
#define ENTTX_ASSERT(condition, msg) (void(0))
#define ENTTX_USER_ASSERT(condition, msg) (void(0))
#elif !defined ENTTX_ASSERT
#include <cassert>
#define ENTTX_ASSERT(condition, msg) assert(((condition) && (msg)))
#define ENTTX_USER_ASSERT(condition, msg)                                      \
  ENTTX_ASSERT(condition, "User assertion failed: " msg)
#endif