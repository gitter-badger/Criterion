set(SAMPLES
  failmessages.c
  exit.c
  long-messages.c
  other-crashes.c

  exit.cc
  long-messages.cc
  other-crashes.cc
)

add_samples("${CMAKE_CURRENT_LIST_DIR}" "${SAMPLES}")
