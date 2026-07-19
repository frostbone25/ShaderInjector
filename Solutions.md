# Solutions / Fixes

This document will occasionally be updated with more information collected from Nexus, mostly from users who found solutions regarding their or others problems. I would like to quickly remind also if you continue to run into issues I would advise reporting them into [Github here](https://github.com/frostbone25/ShaderInjector/issues) as Nexus does not allow attachments of files to bug reports.

**Table of contents**
- [Lyall FF7RebirthFix](#lyall-ff7rebirthfix)
- [Reshade](#reshade)
- [Anti-Virus Shenanigans / False Positives](#anti-virus-shenanigans--false-positives)
- [Shader Adjustments](#shader-configuration-tweaking)
- [Vanishing Lights](#vanishing-lights)
- [Linux Support](#linux-support)
- [Game Not Launching](#game-not-launching)
- [HDR](#hdr)

---

#### [Lyall FF7RebirthFix](https://codeberg.org/Lyall/FF7RebirthFix)
Both mods proxy dsound.dll so normally they can't exist together, but [Zaccachino](https://www.nexusmods.com/profile/Zaccachino) has reported renaming Lyalls dsound.dll to winmm.dll allows both mods to run with each other.

---

#### [Reshade](https://reshade.me/)

ReShade reportedly is unstable for some users. Make sure that you are using the latest version, during testing myself *(using version 6.7.3)* I haven't ran into any issues/crashes with running Injector and Reshade simultaneously. 

However if you're running into issues, [Zaccachino](https://www.nexusmods.com/profile/Zaccachino) reported that you can disable ReShade during the setup process of Shader Injector *(dragging dxgi.dll temporarily out of the game directory)*. When Shader Injector is fully setup you can go into [ShaderInjector.ini](https://github.com/frostbone25/ShaderInjector/blob/main/InjectorSettings.md) and change ```MenuOpen``` so the menu does not open anymore by default *(it helps to change the keybind to a key you might not hit)*. Then re-enable Reshade and now they should be able to co-exist.

---

#### Anti-Virus Shenanigans / False Positives

Reportedly for some users anti-virus flags dsound.dll and sometimes other parts of the mod as "severe" threats *(trojan, etc)*. **This is a false positive.** Unfortunately due to the severity level the Anti-Virus software can delete dsound.dll or some files of ShaderInjector either during installation or when booting the game. 

This can lead to a cascade of issues during the installation/setup of ShaderInjector *(most common one being that shader injector does not ever appear during the setup process)*. **You need to whitelist or disable your Anti-Virus protections** as the false actions it takes can create a whole mess of problems by deleting many of it's files. **Again, it is a false positive it is not malware**, and if you don't trust it the source code is available here on Github and you can inspect the code, or even build the mod yourself.

---

#### [Shader Adjustments](https://github.com/frostbone25/ShaderInjector/blob/main/LiveShaderEditing.md)

The shader comes with many default settings i.e. Contact Shadows, Micro Shadows, BRDF changes, and more. You can tweak or toggle any features within the shader source code HLSL files if things are not to your liking. For example some common ones are...

- Pumping the quality settings for better and more stable shadows. 
- Increasing/Decreasing Ray Length for more or shorter shadow coverage.
- Reducing thickness factor to mitigate false shadowing.
- Swapping BRDF back to game's original Lambert shading.
- Reduce Contact/Micro shadow strength.

I would suggest doing this if you spot artifacts that you may not like brought on either by default settings or different effects *(for example micro shadows is known to introduce overdarkening on some objects)*.

<p float="left">
    <img src="GithubContent/Solutions/pre-processor-macros.png" width="49%" />
</p>

***NOTE:*** *When making shader changes don't forget to Rebuild Replacement Shader to apply changes.*

---

#### Vanishing Lights

For users who are experiencing light sources "vanishing" in interior spaces, if you are not a fan then I would advise disabling the shader replacement for Local Lights. 

Unfortunately this is something that is out of my control as often light sources in the game are placed inside solid objects *(that have shadow casting disabled)*. The side effect is since the mod adds precise shadowing to local lights, those light sources that were visible before now end up appearing blocked because physically... its being blocked by geometry!

#### Linux Support

Courtesy of [smackedwookiee](url=https://www.youtube.com/@SmackedWookiee)!

1) Install directx-shader-compiler
    For Ubuntu:
        sudo apt update
        sudo apt install directx-shader-compiler
    For Arch-based distros:
        sudo pacman -S directx-shader-compiler
    For Fedora
        sudo dnf install directx-shader-compiler
2) Clear the shader cache
    For Steam Users:
        Click on the Settings gear
        Manage > Browse Local Files
        Go up two levels to steamapps
        Navigate to the following path: /compatdata/2909400/pfx/drive_c/users/steamuser/My Documents/My Games/FINAL FANTASY VII REBIRTH/Saved/
        Delete the .ushaderprecache files
    For HGL Users (not tested)
        Click on the game to open the game profile
        Click on the path next to WinePrefix folder
        Navigate to the following path: /drive_c/users/%linuxusername%/My Documents/My Games/FINAL FANTASY VII REBRITH/Saved/
        Delete the .ushaderprecache files
3) Add the following launch options for Steam: WINEDLLOVERRIDES="dsound=n,b"
or for HGL users (not tested)
Add the following Variable name to the game settings page:
Variable name: WINEDLLOVERRIDES
Value: dsound=n,b

---

#### Game Not Launching

Some uesrs have reported that when installing the ShaderInjector that the game wouldn't start. A user has reported that Windows' Smart App Control was blocking the dll from being used, and turning it off fixed it and allowed the game to run.

---

#### HDR

As of 2.0 there are some issues currently with the mod that has come to my attention. For best results until a fix is rolled out I would advise playing in SDR for now. [Your game image may become incredibly dark and very contrasted than intended](https://imgur.com/a/HKkba5E). **This is a bug and there are a number of reasons for this.** Main one being at the moment there's no HDR shader variant of "PostProcessFinal" that I have created. This means many of the image adjustments/tonemaps/auto-exposure won't apply or be usable. This should be resolved in future updates. On a similar note, if you are in SDR I'd advise checking your monitor image settings if you have image issues also. I've made all of these tweaks on calibrated SDR monitors and the mod should not crush the darks of the image so ensure your monitor is properly calibrated and not using image presets that overly punches the image more than it should. Any image adjustments can be done through "PostProcessFinal" which has [wired up parameters](https://github.com/frostbone25/ShaderInjector/blob/main/ConfigurationGuide.md) that you can use to tune the image to your liking.

---

# Resolved Issues

These are issues that are already resolved, but I am keeping them around just in-case there is are some users still running into issues that should have been resolved in future updates

#### (FIXED IN 1.5.1+) Flickering Shadows / Offset Shadows

<p float="left">
    <img src="GithubContent/Solutions/shadow-flicker-on.jpg" width="49%" />
    <img src="GithubContent/Solutions/shadow-flicker-off.jpg" width="49%" />
</p>

*Flickering Shadows*

<p float="left">
    <img src="GithubContent/Solutions/berdrok56-shifted-shadows.png" width="49%" />
</p>

*Offset Shadows*

If you are experiencing some flickering within the distance, or your shadows appear correct but they seem "shifted" either to the left or to the right this is likely due to the fact that you have **Dynamic Resolution enabled within your game settings.** 

There are a couple of ways to fix the issue. The first and simplest one is if you are using Dynamic Resolution it needs to be consistent. A quick fix is to set both your Minimum and Maximum to 50%.

![](GithubContent/Solutions/dynamic-res-50.png)

The other way to fix the issue is to disable Dynamic Resolution entirely setting both the Maximum and Minimum to 100%. 

![](GithubContent/Solutions/dynamic-res-100.png)

Now if you need the performance boost you can instead edit Engine.ini or use [FFVII Hook](https://www.nexusmods.com/finalfantasy7rebirth/mods/4) to natively render at a lower resolution instead using something like ```r.SetRes 1920x1080f```

---