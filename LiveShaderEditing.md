# Live Shader Editing

A note-worthy feature of the Shader Injector is that shaders can be edited and reloaded live during runtime. Whether you are tweaking an existing shader, or creating one from scratch, this should make your workflow far quicker and easier.

### Shader Sources

The raw .hlsl shader source code files are located in ```(game directory)/ShaderInjector/ShaderSources```. 

<p float="left">
    <img src="GithubContent/LiveShaderEditing/directory-game.png" width="24%" />
    <img src="GithubContent/LiveShaderEditing/directory-shader-injector.png" width="24%" />
    <img src="GithubContent/LiveShaderEditing/directory-shader-sources.png" width="24%" />
    <img src="GithubContent/LiveShaderEditing/directory-pixel-shaders.png" width="24%" />
</p>

Now there is multiple sets of folders and potential different shader types for the future, but currently the mod provides 2 modified pixel shaders. These are located inside ```(game directory)/ShaderInjector/ShaderSources/PixelShaders```.

### Editing
You can open the raw .hlsl source code shaders in a text editor *(or code editor)* of your choice and edit them to your liking. The modified pixel shaders provided by the mod are written with pre-processor macros that are wired up, where you can easily toggle or adjust certain shader features.

![shader-code-example](GithubContent/LiveShaderEditing/shader-code-example.png)
*LocalLightShader.hlsl written with macros at the very top of the file*

If you are an experienced programmer you can skip this explanation, but for those who arent heres a very quick and brief rundown to configure things within a shader source code...

Enabling/Disabling a shader feature

```HLSL
//feature is enabled
#define FEATURE   

//feature is disabled
//#define FEATURE
```

Changing a value
```HLSL
//a define variable set with a value
#define FEATURE_VALUE 1.0

//the value can be changed
#define FEATURE_VALUE 100.0
```

Make sure to save changes before you [reload](#reloading-changes) in Shader Injector.

### Reloading Changes

Once you've made a change, tab or go back into the game and under ```Shader Replacements``` menu. Then select the shader replacement that is pointing to the shader that you are editing, in my case ```LocalLightShader.hlsl```, and click simply ```Rebuild Shader Replacement```. Your changes should be reflected immedieatly.

<p float="left">
    <img src="GithubContent/LiveShaderEditing/reload-shader-a.png" width="32%" />
    <img src="GithubContent/LiveShaderEditing/reload-shader-b.png" width="32%" />
</p>

If your changes are not being reflected or updated, there is a good chance you might be hitting a compilation error *(or something similar)*. Check under the ```Logs``` section within the shader injector menu, and correct the issues. 

![compile-error](GithubContent/LiveShaderEditing/compile-error.png)

*NOTE: Currently the shader injector does not provide the exact compilation errors in the logs, this should be improved on future updates.*

### Examples

Some examples to illustrate

<p float="left">
    <img src="GithubContent/LiveShaderEditing/example-reload1-a.jpg" width="32%" />
    <img src="GithubContent/LiveShaderEditing/example-reload1-b.jpg" width="32%" />
    <img src="GithubContent/LiveShaderEditing/example-reload1-c.jpg" width="32%" />
</p>

*Example: Tinting the light color within the ```LocalLightShader.hlsl```* *(NOTE: These are older screenshots with an older UI)*
