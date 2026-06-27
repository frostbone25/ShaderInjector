# Shader Injector
Shader Injector Mod for [FF7 Rebirth PC](https://store.steampowered.com/app/2909400/FINAL_FANTASY_VII_REBIRTH/). This is a DX12 *(D3D12)* based mod that intercepts the rendering API calls, and allows you to inject/replace the original shaders within the title with modified ones during runtime. This mod was built for [FF7 Rebirth](https://store.steampowered.com/app/2909400/FINAL_FANTASY_VII_REBIRTH/), however because it's primarly just a DX12 interceptor, theroetically this can be adapted to work with other D3D12 based titles.

[More technical details here.](https://frostbone25.github.io/p/ff7-rebirth-contact-shadows/#timings-on-1920x1080-and-3840x2160)

---
# Download
### [Nexus Mods](https://www.nexusmods.com/finalfantasy7rebirth/mods/2153)
### [Github](https://github.com/frostbone25/ShaderInjector/releases)

---
# Guides
### [Installation Guide](https://github.com/frostbone25/ShaderInjector/blob/main/INSTALL.md)
### [ShaderInjector.ini](https://github.com/frostbone25/ShaderInjector/blob/main/InjectorSettings.md)
### [Live Shader Editing](https://github.com/frostbone25/ShaderInjector/blob/main/LiveShaderEditing.md)
### [How to report an issue](https://github.com/frostbone25/ShaderInjector/blob/main/IssueReport.md)
### *Building a Shader (coming soon...)*
---
# Showcase

- [Video Preview](https://www.youtube.com/watch?v=ta1WpIeoP1s)
- [Video Gameplay](https://youtu.be/4dG2Av6-of4)
- [Video Installation](https://youtu.be/5k9GTPK2eM0)

<p float="left">
    <img src="GithubContent/Screenshots/3-on.jpg" width="49%" />
    <img src="GithubContent/Screenshots/3-off.jpg" width="49%" />
</p>

*Left Side: Shader Injector On | Right Side: Shader Injector Off | (3840x2160)*

<p float="left">
    <img src="GithubContent/Screenshots/2-on.jpg" width="49%" />
    <img src="GithubContent/Screenshots/2-off.jpg" width="49%" />
</p>

*Left Side: Shader Injector On | Right Side: Shader Injector Off | (3840x2160)*

---
# Issues

If you find any issues with the mod or general shader injector framework please report an [issue here on GitHub](https://github.com/frostbone25/ShaderInjector/issues), or whevever the mod is hosted. **Please be as descriptive as possible when reporting an issue, otherwise it will be ignored!**

In addition if you would like to do pull requests to improve or contribute features to Shader Injector feel free to do so!

---
# TODO
- General Bug / Issue Fixing
- Compute / Vertex Shader Replacements
- Automated Shader Replacement Searching *(eliminating the setup steps of manual search for shaders)*
- Improve "stability" in regards to GPU/Driver updates by parsing shader bytecode directly in memory and obtaining unique shader and other PSO characteristics to identify a specific shader reliably across machines for automated shader finding/replacement.
- Linux Support

---
# Sources / References
- [minhook (TsudaKageyu)](https://github.com/TsudaKageyu/minhook)
- [imgui (ocornut)](https://github.com/ocornut/imgui)
- [json (nlohmann)](https://github.com/nlohmann/json)
- [inifile-cpp (Rookfighter)](https://github.com/Rookfighter/inifile-cpp)
- [Universal-Dear-ImGui-Hook (Sh0ckFR)](https://github.com/Sh0ckFR/Universal-Dear-ImGui-Hook/tree/master)

# Special Thanks

People who have aided in the development of this project with testing or other various tasks, big or small.

- [Neocortex](https://www.nexusmods.com/profile/Neocoretx)
- Stephe
- [YIIS](https://www.nexusmods.com/profile/YIISx/mods)
- smackedwookiee
- sajittarius