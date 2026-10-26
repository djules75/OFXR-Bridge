# V576: the tray's POST_BUILD copy of the Win32 build's layer into ofxr_x86
# beside its ofxr folder. Nothing to copy until that build exists.
if(EXISTS "${SOURCE}")
    file(MAKE_DIRECTORY "${DESTINATION}")
    file(COPY_FILE "${SOURCE}"
        "${DESTINATION}/XR_APILAYER_XRFrameBridge_diagnostic.dll"
        ONLY_IF_DIFFERENT)
endif()
