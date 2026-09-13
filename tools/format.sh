find /home/vscode/workspace/psdk/samples/sample_c++/platform/linux/cy_psdk \
    -type f \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) \
    -print0 | xargs -0 clang-format -i --style=file
