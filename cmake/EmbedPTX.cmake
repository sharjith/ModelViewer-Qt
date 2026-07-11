# Runs bin2c on a compiled .ptx file and writes the resulting byte array as an
# `extern const char* const <VARIABLE_NAME>` C++ string constant into
# CPP_FILE - our own minimal equivalent of the OptiX SDK's own ptx2cpp.cmake,
# invoked as a CMake script (cmake -P) from a custom command in
# CMakeLists.txt so the embedding step runs at build time, once the .ptx
# actually exists.
#
# Expected -D arguments: BIN2C_EXECUTABLE, PTX_FILE, CPP_FILE, VARIABLE_NAME

execute_process(
    COMMAND "${BIN2C_EXECUTABLE}" -p 0 -st -c -n "${VARIABLE_NAME}_bytes" "${PTX_FILE}"
    OUTPUT_VARIABLE ptx_bytes
    RESULT_VARIABLE bin2c_result
    ERROR_VARIABLE bin2c_error
)
if(bin2c_result)
    message(FATAL_ERROR "bin2c failed on ${PTX_FILE}:\n${bin2c_error}")
endif()

file(WRITE "${CPP_FILE}"
    "${ptx_bytes}\n"
    # 'extern' is required here, not just in the header declaration - a
    # top-level `const` in C++ has internal linkage by default (unlike C),
    # so without it this definition would be invisible to every other
    # translation unit (RtOptixTracer.cpp would fail to link against it).
    "extern const char* const ${VARIABLE_NAME} = reinterpret_cast<const char*>(&${VARIABLE_NAME}_bytes[0]);\n"
)
