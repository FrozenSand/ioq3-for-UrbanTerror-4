# ioQuake 3 for UrbanTerror

[![Build Status](https://travis-ci.org/mickael9/ioq3.svg?branch=urt)](https://travis-ci.org/mickael9/ioq3)
[Download prebuilt binaries for Linux/macOS/Windows](http://ioq3urt.mickael9.tk/urt/)

This project is an initiative to backport the relevant ioUrbanTerror-specific
features to an up-to-date ioQuake3.

This brings all of the ioquake3 features to UrbanTerror:
 - [VOIP support](voip-readme.txt) from ioquake3
 - Mumble support
 - New [OpenGL2 renderer](opengl2-readme.md)

And a lot of other things, see the [ioquake3 README](README.ioq3.md)

# New features

Those are the features that are specific to this build.

## No more map conflicts!
This build includes a few features to address the map conflict issues that can
be encountered when playing Urban Terror.

If you have a lot of third-party maps in your download folder and tried to play
in single player, you might have noticed some issues, like say the SR8 being
different in aspect, or different sounds, invalid textures, etc.

This is caused by the way quake3 search path works: when a file is requested,
it goes through the quake3 search path until the file is found, so a file that
exists higher in the search path will always "shadow" lower priority files.
Since there is no concept of "map file", you can have situations where a
ressource exists in the map pk3 but gets picked from another pk3. Some maps
also like to override default ressources (like weapon models, flags, sounds,
etc.)

### `fs_reorderPaks` cvar

When set to 1 (the default), this will reorder the pk3s in the search path so
that the pk3 containing the loaded map comes first, then the core game paks and
finally everything else.

This way:

 - The map always has priority over everything else, respecting the mapper's
   intention.

 - Other maps can't override core ressources from the game paks

 - Additional ressources (like funstuff) can still be loaded from the other
   pk3s

This cvar works on clients and servers :

 - Pure servers with this cvar will affect the clients since they respect the
   search path order from the server.  This is not the case on unpure servers
   so this option will have no practical effect on unpure servers.

 - Clients with this cvar on will always benefit, wether they connect to pure
   or unpure servers.  When connecting to pure servers, the reordering is done
   after the server search order is copied

### `sv_extraPure` and `sv_extraPaks`
When set to 1, this will make the server "extra pure". The loaded paks list
sent to clients will be reduced to include only the core game paks as well as
the referenced paks (ie the map pk3). The main use for this is if you
have so many loaded paks that the server can't work in pure mode (because the
list of loaded paks is too big to fit in the server info string).

This also means that no paks other than the map pak will be loaded by clients,
disabling usage of, for instance, funstuff. This can be solved by setting the
`sv_extraPaks` cvar to a space-separated list of pk3 names that you don't want
to exclude from the search path

### `fs_lowPriorityDownloads`
When set to 1 (the default), this puts the maps in the `download` folder at a
lower priority than anything else.

## Other changes
 - Download UI is improved a bit
 - Downloading can still be attempted if the server has no download URL set.
   In this case we use the default one (urbanterror.info).
 - Number keys in the first row are always mapped to number keys, on AZERTY
   layouts for instance. This matches with the behavior of ioUrbanTerror on
   Windows and brings it to other platforms as well.
 - Numpad `2` and `8` keys aren't interpreted as up/down in console anymore.
 - `+vstr` supports nested key presses

## Security fixes
 - QVMs, `q3config.cfg` and `autoexec.cfg` can't be loaded from downloaded pk3s
   anymore.

# Feature parity status with original ioUrbanTerror

## Common
- [x] UrbanTerror 4.2+ demo format (.urtdemo)
- [x] Auth system
- [x] `+vstr` command

## Client
- [x] Make `/reconnect` work across restarts
- [x] Use `download` folder for downloads
- [x] Disallow QVMs in download folder
- [x] Fancy tabbed console
- [x] dmaHD
- [x] Prompt before auto download
- [ ] Alt-tab and `r_minimize` cvar
- [x] New mouse acceleration style (`cl_mouseAccel 2`)
- [x] Make the client query other master servers if main one does not respond
- [ ] Client commands changes/additions
   - [x] Escape `%` in client to server commands (allows usage of '%' in chat)

## Server
- [x] Server demos
- [x] `sv_clientsPerIP` cvar
- [x] `sv_sayprefix` / `sv_tellprefix` cvars
- [x] Send UrT specific server infostring
- [ ] Server commands changes/additions
   - [x] Partial matching of map and players

Please let me know if I forgot anything from the list!
