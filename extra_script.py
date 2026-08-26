"""PlatformIO 的构建前置脚本。

作用只有一个：生成 compile_commands.json。
这个文件是给编辑器用的，里面记着每个源文件的编译参数。
有了它，clangd 才能正确跳转定义、补全代码、报错。

脚本在编译开始前自动执行，不需要手动调用。
"""
import os
Import("env")

# 把工具链的头文件路径也写进去。
# 不加这一项，编辑器会找不到 HAL 库和标准库的头文件，到处报红。
env.Replace(COMPILATIONDB_INCLUDE_TOOLCHAIN=True)

# 把输出位置指到构建目录里，避免污染工程根目录。
env.Replace(COMPILATIONDB_PATH=os.path.join("$BUILD_DIR", "compile_commands.json"))
