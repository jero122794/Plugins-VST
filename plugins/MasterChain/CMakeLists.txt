add_studio_plugin(TARGET MasterChain PRODUCT "MasterChain" CODE Mch1 VST3_CATEGORY Mastering
    CLAP_FEATURES mastering limiter compressor equalizer analyzer
    SOURCES Source/ChainProcessor.cpp Source/ChainEditor.cpp
    LIBS mastering)

add_processor_tests(TARGET MasterChain_ProcessorTests PLUGIN_NAME "MasterChain"
    SOURCES tests/ChainTests.cpp Source/ChainProcessor.cpp Source/ChainEditor.cpp
    LIBS mastering)
