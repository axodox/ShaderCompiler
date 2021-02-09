# ShaderCompiler

Generate multiple shader variants from HLSL.

# Command line usage

`ShaderGenerator.exe`

- `-i=<file_path>`: Path of the source code
- `-o=<dir_path>`: Path of the output directory
- `-h=<dir_path>`: Path of the include header
- `-d`: Debug mode with debug symbols

# Source file usage

```hlsl
#pragma target cs_5_0 //Compilation target
#pragma entry main //Entry point - optional, default is 'main'
#pragma namespace MyApp::Shaders //Namespace for include header
#pragma option bool IsSomethingEnabled //A boolean option
#pragma option enum RenderMode {X, Y, Z} //An enum option
#pragma option int SampleCount {1..4} //An integer option
```
