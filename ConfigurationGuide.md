# Configuration Guide

*NOTE: This was written at the release of 2.0, with newer updates this might become more out of date but the general principles are the same.*

With the release of 2.0 it comes with a whole suite of new shaders and features that drastically improve the lighting quality of the game! **However...**

**Some of the features and effects that are featured in screenshots or videos that you might have seen are disabled by default.** This is done for various reasons, the main one being performance. Some of these effects are still experimental and are quite heavy at the moment, I have done my best to optimize but to go further will require further updates in the future to make them much lighter to run. In addition some of them may have some visual problems. For instance the SSGI can add a lot of noise to the final image, or the Auto Exposure can flicker quite a bit.

With that said, even in my sessions I find most of the issues managable and performance on my system *(RTX 3080)* at native 1080p is acceptable. If you want the full visual splendor I will guide you on where to enable the features!

<p float="left">
    <img src="GithubContent/LiveShaderEditing/directory-game.png" width="24%" />
    <img src="GithubContent/LiveShaderEditing/directory-shader-injector.png" width="24%" />
    <img src="GithubContent/LiveShaderEditing/directory-modified-shaders.png" width="24%" />
    <img src="GithubContent/LiveShaderEditing/directory-includes.png" width="24%" />
</p>

The raw .hlsl shader source code files are located in ```(game directory)/ShaderInjector/ModifiedShaders```. 

# Maximum Visual Quality Configuration

By default as of 2.0 SSGI and it's AO counterpart along with auto exposure are disabled. To match the visual fidelity as seen in promotional screenshots and videos heres how to enable them.

### SSGI / AO

Find the following file....

```
~FINAL FANTASY VII REBIRTH\End\Binaries\Win64\ShaderInjector\ModifiedShaders\Includes\ComputeShaderPass_ReflectionEnvironment.hlsl
```

Open this file in a text/code editor and you'll find the following fields...

```GLSL
//#define SSGI_AMBIENT_OCCLUSION

//#define SSGI_BOUNCE_LIGHT
```

To enable them, just simply get rid of the two forward slashes on each of them like so...

```GLSL
#define SSGI_AMBIENT_OCCLUSION

#define SSGI_BOUNCE_LIGHT
```

Save changes to the file and tab or open the game back up, and click ```Recompile All```.

![recompile-all](GithubContent/LiveShaderEditing/recompile-all.png)

You should see immediate visual changes after compilation completes, with more visible ambient occlusion and local bounce light!

#### SSGI / AO Quality Notes

Currently as of 2.0 these effects, especially ```SSGI_BOUNCE_LIGHT``` can be quite noisy. I plan to improve upon this in the future by introducing dedicated draw passes to filter and downsample the effects for performance/image quality but your kind of limited in terms of how to deal with the noise. 

With that said there are some things you can try.

- Increase Rendering Resolution: This will make the impact of the SSGI signifcantly higher because it scales with screen resolution, but more pixels means the noise becomes smaller and the final result appears cleaner.
- Increase Raymarch Sample Counts: This will improve the quality of the raymarch and reduce noise but of course at a greater cost.
- Update Game's DLSS Preset: I have noticed while testing on multiple game versions that 1.0.0.5 seems to have an updated DLSS variant that actually led to reduced noise when SSGI was enabled. I would experiment with this as a potential avenue for improving the noise situation.

### Auto Exposure

Find the following file....

```
~FINAL FANTASY VII REBIRTH\End\Binaries\Win64\ShaderInjector\ModifiedShaders\Includes\PixelShaderPass_PostProcessFinal.hlsl
```

Open this file in a text/code editor and you'll find the following fields...

```GLSL
//#define AUTO_EXPOSURE
```

To enable them, just simply get rid of the two forward slashes on each of them like so...

```GLSL
#define AUTO_EXPOSURE
```

Save changes to the file and tab or open the game back up, and click ```Recompile All```.

![recompile-all](GithubContent/LiveShaderEditing/recompile-all.png)

You should see immediate visual changes after compilation completes, with more visible ambient occlusion and local bounce light!

### Tonemapping

Find the following file....

```
~FINAL FANTASY VII REBIRTH\End\Binaries\Win64\ShaderInjector\ModifiedShaders\Includes\PixelShaderPass_PostProcessFinal.hlsl
```

Open this file in a text/code editor and you'll find the following fields...

```GLSL
#define TONEMAP_PRESERVE_COLOR_GRADE

//#define TONEMAP_NONE
//#define TONEMAP_GRAN_TURISMO_7
//#define TONEMAP_AGX
//#define TONEMAP_UCHIMURA
//#define TONEMAP_REINHARD
//#define TONEMAP_REINHARD2
//#define TONEMAP_UNCHARTED2
//#define TONEMAP_ACES
//#define TONEMAP_ACES_FITTED
//#define TONEMAP_FILMIC
//#define TONEMAP_UNREAL_3
//#define TONEMAP_KHRONOS_NEUTRAL
//#define TONEMAP_LOTTES
//#define TONEMAP_EXPONENTIAL
//#define TONEMAP_EXPONENTIAL_SQUARED
//#define TONEMAP_MGSV
//#define TONEMAP_TONY_MC_MAP_FACE
```

There are many different tonemappers to choose from. You can experiment but the ones I use in my videos/screenshots that is a personal favorite of mine *(and in my opinon the superior one out of all of them)* I use ```TONEMAP_GRAN_TURISMO_7```.

```GLSL
#define TONEMAP_PRESERVE_COLOR_GRADE

//#define TONEMAP_NONE
#define TONEMAP_GRAN_TURISMO_7
//#define TONEMAP_AGX
//#define TONEMAP_UCHIMURA
//#define TONEMAP_REINHARD
//#define TONEMAP_REINHARD2
//#define TONEMAP_UNCHARTED2
//#define TONEMAP_ACES
//#define TONEMAP_ACES_FITTED
//#define TONEMAP_FILMIC
//#define TONEMAP_UNREAL_3
//#define TONEMAP_KHRONOS_NEUTRAL
//#define TONEMAP_LOTTES
//#define TONEMAP_EXPONENTIAL
//#define TONEMAP_EXPONENTIAL_SQUARED
//#define TONEMAP_MGSV
//#define TONEMAP_TONY_MC_MAP_FACE
```

Save changes to the file and tab or open the game back up, and click ```Recompile All```.

![recompile-all](GithubContent/LiveShaderEditing/recompile-all.png)

You should see immediate visual changes after compilation completes, with different tonal range and better color accuracy than the base game!