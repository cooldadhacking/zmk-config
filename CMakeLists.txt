# In-repo ZMK module providing the &mock_word behavior (random caps, like
# caps_word but mocking-SpongeBob case). The build workflow auto-adds this
# repo as a ZMK extra module when it sees zephyr/module.yml at the root.
zephyr_library()
zephyr_library_sources(src/behavior_mock_word.c)
