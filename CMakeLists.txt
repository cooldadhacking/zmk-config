# In-repo ZMK module providing the &mock_word behavior (random SpongeBob caps).
# The build workflow auto-adds this repo as a ZMK extra module when it sees
# zephyr/module.yml at the root.
#
# Add the source to the `app` target (not a standalone zephyr_library) so it
# inherits ZMK's app include dirs -- needed for <drivers/behavior.h>, which is
# NOT exported to modules via app/module/include. Mirrors urob/zmk-auto-layer.
# Behaviors run on the split central, so only build there.
if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  target_sources(app PRIVATE src/behavior_mock_word.c)
endif()
