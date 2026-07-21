cmake_minimum_required(VERSION 3.16)
project(TestPlugins LANGUAGES CXX)

add_executable(motionplayer-dll
    main.cpp
    motionplayer-dll.cpp
    registry.cpp
    kbad-data-pack.cpp
    textrender-timing.cpp
    text-stream.cpp
    xp3-cx-decoder.cpp
    font-compat.cpp
    font-baseline.cpp
    godot-texture.cpp
    krkrgles-aliases.cpp
    kag-load-contract.cpp
)

set(_KRKR2PLUGIN_LINK_ITEM "$<LINK_LIBRARY:WHOLE_ARCHIVE,krkr2plugin>")

target_link_libraries(motionplayer-dll
    PRIVATE
        Catch2::Catch2
    PUBLIC
        krkr2core
        ${_KRKR2PLUGIN_LINK_ITEM}
)

target_include_directories(motionplayer-dll PRIVATE
    "${TEST_CONFIG_DIR}"
    "${CMAKE_SOURCE_DIR}/cpp/plugins"
)

catch_discover_tests(motionplayer-dll)

if(AETHERKIRI_INTERNAL_ENABLED)
    # Compile the exact public motionplayer test source against the private
    # namespace as a compatibility contract. New public API/tests therefore
    # constrain internal builds automatically.
    add_executable(motionplayer-internal-contract-dll
        main.cpp
        motionplayer-dll.cpp
    )
    aetherinternal_configure_motionplayer_contract_target(
        motionplayer-internal-contract-dll)
    target_link_libraries(motionplayer-internal-contract-dll
        PRIVATE
            Catch2::Catch2
        PUBLIC
            krkr2core
            ${_KRKR2PLUGIN_LINK_ITEM}
    )
    target_include_directories(motionplayer-internal-contract-dll PRIVATE
        "${TEST_CONFIG_DIR}"
        "${CMAKE_SOURCE_DIR}/cpp/plugins"
    )
    catch_discover_tests(motionplayer-internal-contract-dll
        TEST_PREFIX "internal-contract::")

    # Run private-only E-mote behavior tests in addition to both shared suites.
    add_executable(motionplayer-internal-dll
        main.cpp
    )
    aetherinternal_configure_motionplayer_tests(
        motionplayer-internal-dll)
    target_link_libraries(motionplayer-internal-dll
        PRIVATE
            Catch2::Catch2
        PUBLIC
            krkr2core
            ${_KRKR2PLUGIN_LINK_ITEM}
    )
    target_include_directories(motionplayer-internal-dll PRIVATE
        "${TEST_CONFIG_DIR}"
        "${CMAKE_SOURCE_DIR}/cpp/plugins"
    )
    catch_discover_tests(motionplayer-internal-dll
        TEST_PREFIX "internal::")
endif()
