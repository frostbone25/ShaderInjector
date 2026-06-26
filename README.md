# Shader Injector
Shader Injector Mod for [FF7 Rebirth PC](https://store.steampowered.com/app/2909400/FINAL_FANTASY_VII_REBIRTH/). This is a DX12 *(D3D12)* based mod that intercepts the rendering API calls, and allows you to inject/replace the original shaders within the title with modified ones during runtime. This mod was built for [FF7 Rebirth](https://store.steampowered.com/app/2909400/FINAL_FANTASY_VII_REBIRTH/), however because it's primarly just a DX12 interceptor, theroetically this can be adapted to work with other D3D12 titles.

---
# Download
### [Github](https://github.com/frostbone25/ShaderInjector/releases)
### Nexus Mods *(Not Up Yet)*

---
# Guides
### [Installation Guide](https://github.com/frostbone25/ShaderInjector/blob/main/INSTALL.md)
### [ShaderInjector.ini](https://github.com/frostbone25/ShaderInjector/blob/main/InjectorSettings.md)
### [Live Shader Editing](https://github.com/frostbone25/ShaderInjector/blob/main/LiveShaderEditing.md)
### *Building a Shader (will come soon...)*
---
# Screenshots

---
# TODO:
- General Bug / Issue Fixing
- Compute / Vertex Shader Replacements
- Automated Shader Replacement Searching *(eliminating the setup steps of manual search for shaders)*
- Improve "stability" in regards to GPU/Driver updates by parsing shader bytecode directly in memory and obtaining unique shader characteristics to identify a specific shader reliably across machines for automated shader finding/replacement.
- Linux Support

---
# Sources / References
- [minhook (TsudaKageyu)](https://github.com/TsudaKageyu/minhook)
- [imgui (ocornut)](https://github.com/ocornut/imgui)
- [json (nlohmann)](https://github.com/nlohmann/json)
- [inifile-cpp (Rookfighter)](https://github.com/Rookfighter/inifile-cpp)
- [Universal-Dear-ImGui-Hook (Sh0ckFR)](https://github.com/Sh0ckFR/Universal-Dear-ImGui-Hook/tree/master)