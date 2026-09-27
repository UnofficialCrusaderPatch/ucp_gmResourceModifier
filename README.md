# UCP Gm Resource Modifier

This repository contains a module for the "Unofficial Crusader Patch Version 3" (UCP3), a modification for Stronghold Crusader.
This module exposes the possibility to modify the loaded gm1 resources of the game.


### Motivation and Plan

The gm files of Crusader are handled differently than the tgx files.
While the later are loaded from disc on demand, the former are loaded at the start. (They might be a big part or even majority of the loading bar.)
Replacing them requires more than intercepting and changing paths.

For this reason, the control structures of the stored gm files were identified to replace them on demand.
In more detail, this module allows to load gm1-files, which are then stored as resources.
If requested and the wanted replacement fits, the control structures of the vanilla file are copied and then replaced with the other resource.
The main byte array is not touched. The replacement pointer simply points into the loaded resource instead.
Resetting is also possible and so it freeing the loaded resources (if they are not used anymore).

One can replace either a whole file or individual pictures. They need to be of the same type, though, since SHC stores different formats in a gm1 file.

As an additional feature, one can use this module to create a interface type single picture resource from a picture.
The converter calls Windows functions, so the supported depends on the OS (and what Wine supports).


### Usage

The module is part of the UCP3. Certain commits of the main branch of this repository are included as a git submodule.
It is therefore not needed to download additional content.

However, should issues or suggestions arise that are related to this module, feel free to add a new GitHub issue.
Support is currently only guaranteed for the western versions of Crusader 1.41 and Crusader Extreme 1.41.1-E.
Other, eastern versions of 1.41 might work.


### C++-Exports

The API is provided via exported C functions and can be accessed via the `ucp.dll`-API.  
To make using the module easier, the header [gmResourceModifierHeader.h](ucp_gmResourceModifier/ucp_gmResourceModifier/gmResourceModifierHeader.h) can be copied into your project.  
It is used by calling the function *initModuleFunctions()* in your module once after it was loaded, for example in the lua require-call. It tries to receive the provided functions and returns *true* if successful. For this to work, the *gmResourceModifier* needs to be a dependency of your module.
The provided functions are the following:

* `int LoadGm1Resource(const char* filepath)`  
  Create a resource by trying to load the gm1-file the path points to.
  Returns the id of the resource to use for the other functions or `-1`, if it fails.
  Save the id! If it gets lost, one ends up with leaked memory in a way.

* `bool SetGm(int gmID, int imageInGm, int resourceId, int imageInResource)`  
  This options sets replaced gm data using the loaded resources. It takes different parameters:
    * The `gmID` is the index of the gm file in the games memory.  
    * The `imageInGm` is the index of the picture in the gm file.  
    * The `resourceId` is the id of the loaded resource.  
    * The `imageInResource` is the index of the picture in the resource file.  
  
  The actual action depends on the given parameters. In all cases the not mentioned parameters are `-1`:
    * `gmID`: Resets the requested gm-data to vanilla.
    * `gmID`, `imageInGm`: Resets the requested image in the gm-data to vanilla.
    * `gmID`, `resourceId`: Replaces the gm with the resource, if possible.
    * `gmID`, `imageInGm`, `imageInGm`, `resourceId`: Replaces the specific image, if possible.
  
  The return value indicates if it was successful.

* `bool FreeGm1Resource(int resourceId)`  
  Frees the resource, but only if it is used by nothing.
  The return value indicates if it was successful.

* `int LoadResourceFromImage(const char* filepath)`  
  Similar to `LoadGm1Resource`, but takes a path to an image file.
  If possible, the image is transformed to a single or multi(gif) picture resource of the interface type.
  It can therefore only be used by this or compatible types.
  The supported formats depend on the OS. 


### Lua-Exports

Version 0.3.0 also exposes inherited-sheet reservations in both Lua and C++:

* `int ReserveGm(int baseGm, int resourceId)` is called before native GM loading.
  It pins a resource loaded with `LoadGm1Resource` and returns a reservation token
  or `-1`. The resource must match the base sheet's type and image count. The token
  is **not** a native sheet ID. Reservations are allocated in request order.
* `int GetReservedGm(int token)` returns the admitted native sheet ID after
  initialization, or `-1` before initialization/on failure. Consume this result
  through the framework's existing `hooks.registerHookCallback("afterInit", ...)`.
  A required consumer must stop startup on failure; that hook catches ordinary
  Lua errors, so an assertion alone is insufficient. The existing framework
  `log(FATAL, ...)` path terminates startup.

Admission validates the whole pending batch before writing any reserved native
layout. Invalid bases, incompatible layouts or exhausted 240-sheet/66000-image
capacity reject the whole batch. Resource pins are released when initialization
finishes, including failure. On success, `SetGm` retains the ordinary replacement
references. Resetting the allocated sheet restores the inherited **native** base
sheet; it does not deallocate or recycle its ID. Existing texture replacements
keep their request order and are applied before reserved custom sheets. Failed
resources can be freed normally. Reserve only during startup, on the game thread.

The API supplies sheet storage, not a new projectile type or renderer. Consumers
must retain their native animation/flight/damage owners. No capacity is expanded.
See [the acceptance record](tests/README.md) for current verification limits.

Version 0.3.1 adds `LoadCompleteGm1Resource(path, imageCount, gm1Type)` to the Lua
module. It returns the resource ID and a lowercase SHA-256 digest of the exact
loaded bytes, or `-1, nil` on failure. It accepts only a complete, self-contained
sheet of GM1 type 1, 2 or 6 with the requested layout and validates image bounds and token streams
before preparing native resources. The file is read once by this owner; consumers
can use the digest in save/config identity and pass the returned ID to `ReserveGm`.
The existing `LoadGm1Resource` remains available for partial texture replacement.

The Lua exports are parameters and functions accessible through the module object.

* `int LoadGm1Resource(string filepath)`  
  Identical to C++ version.

* `bool SetGm(int gmID, int imageInGm, int resourceId, int imageInResource)`  
  Identical to C++ version.

* `bool FreeGm1Resource(int resourceId)`  
  Identical to C++ version.

* `int LoadResourceFromImage(string filepath)`  
  Identical to C++ version.

### Special Thanks

To all of the UCP Team, the [Ghidra project](https://github.com/NationalSecurityAgency/ghidra) and
of course to [Firefly Studios](https://fireflyworlds.com/), the creators of Stronghold Crusader.
