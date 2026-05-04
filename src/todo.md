- add "prev/next chapter" button
DONE: add separators or box highlights around hovered error in formatted error list
- check the !qb files in the manga folder
- crop errored placeholder image to square and render it with the same aspect ratio always with padding
- generate array of mipmaps with vips_thumbnail and change Image to store a vector of std::pair<std::size_t, unsigned char*>
- keep the (digital) (1r0n) stuff but only add it in a tooltip when hovering a manga name in the list
- look into fixing the archives with errors (maybe email the creator??)
- make the glTexImage2D upload faster (maybe keep deleted buffers and reuse? but if the size differs, what's the point yk. maybe just keep the last three)
- optional tab layout
- other cosmetic changes
- redirect stdio to internal buffer/ostream and add a logging panel
- remove "One Punch Man\Replaced" & "My Love Story With Yamada-...\Chapters" (and all "manga" in a folder that has cbz files already)
- remove dependency on stb_image and refactor it to use libvips (or at least the intensive image loading for the pages)
- segfault when rapidly changing image cache upscaling factor from 4 to 1 (maybe to zero? crashed as slamming it to the left)
- show manga path in tooltip or disabled style text in manga list
- web api backend replacement for images.cpp?
- webasm?? eyes emoji?
- write some code to hash and check the .cbz[1], .cbz[2] and so on for equality to the .cbz equivalents (and maybe check if any torrent is tracking them in qbt)
DONE: actually test the new and improved imagecache and, to do so, implement the reader and inspection window
DONE: add a check that all files in an archive have the same depth from the root (so if they're all like archive/*.jpg or archive/chapter/*jpg, it's fine but a mix of levels isn't)
DONE: add a global error struct to the manga.zip output and then have the error_type combo menu take a `const Errors*` as a param to display error counts with the names
DONE: add a one or two page reading mode as opposed to the scrolling mode
DONE: add indexer.exe to the get_deps.sh script
DONE: add is_moving_window flag or something so moving too fast doesn't randomly quit when your mouse moves off the titlebar
DONE: add manual scrollbar to reader
DONE: add metadata info for chapters as treenodes in the manga details panel
DONE: add metadata info for pages in context menu: author, dimensions, channels, file size, etc
DONE: allow multiple reader/error/json/inspect windows
DONE: block scroll past the end
DONE: bring all windows that aren't the root window to front when they're created
DONE: check for all pages in chapter have the same volume field (either None or all the same)
DONE: clicking past the end of the chapter opens the last page?
DONE: context menu for manga list and manga details to copy path
DONE: continue fucking fucking gfettging fuvcking jq static fgufcking livbrayr
DONE: copy to clipboard for errors
DONE: customizable filter min score for manga list
DONE: display chapter listing as v03 - ch13 if all pages in chapter have the same volume (maybe display the chapters under the volume in a treenode?)
DONE: filter manga list to show only manga with errors (maybe of a certain type)
DONE: fix bad nonoptional error filter (filters by the first option if not provided??)
DONE: fix sorting of mixed volumes and chapters: (<volume-only>..., <sorted only by chapter>...)
DONE: fix the ID-less text/image issue
DONE: fix the blocking open chapter function which i cannot understand
DONE: formatted error list has id collisions and overlapping context menus
DONE: gl not clamping to border but to edge?
DONE: handle (-1, -1, -1) extents for images
DONE: icon for the executable
DONE: if files in archive are formatted as <archive name>/Chapter <ch>/\d+.jpg with multiple chapters, keep them separate
DONE: images in reader are slightly cropped off-screen on the right side (probably calculating the width and not accounting for the scrollbar)
DONE: jq subprocesses crash on launch (sethandleinformation fails when setting permissions to 'inherit')
DONE: jq write to pipe fails and results in a `bad literal at ... EOF` error in jq output
DONE: jq-like explorer for raw json
DONE: keep trying to compile for WSL and hopefully get closer to jq integration
DONE: lazy-load the bulk of the data so that startup is faster and the memory requirement are lower
DONE: less monolithic source files
DONE: make subprocess routine cross-platform and also just generally clean up that code
DONE: max path length fucked up loading images! fuckass LN titles are too long! can't fucking open them! think the issue is with the zip.h library, maybe try libzip?
DONE: maybe parse comick's `(\d+)-(<junk hash data>)` in revelation of youth
DONE: more lock-free thread communication
DONE: parse `<manga name> v(\d+) - p(\d+)...` by Ushi
DONE: parse `<manga name> v(\d+) p(\d+)...` by Vodka
DONE: parse `Chapter (\d+)/(\d+)` by anonymous  (merge this with the above Volume 01/ syntax)
DONE: parse `Volume (\d+)/v(\d+)_(\d+)(?:-(\d+))?`
DONE: parse `i_(\d+)...`
DONE: parse archive name `<manga name> - Vol.(\d+)` in Veil (should rename v07 so the prefix include everything up to "Vol.")
DONE: parse archive names "v01 c10" without erroring due to collisions with "v02 c5" (maybe we need to treat collections with both volume and chapter as different from just volume)
DONE: parse page ranges separated by ~ by Ushi
DONE: per-chapter error display on the manga details panel
DONE: placeholder images (unloaded & bad image) are displayed as pure black? 
DONE: relative path for manga is currently just set to the manga name. this is just wrong broken heart emoji, i need to actually do some logic to get the relpath
DONE: reload data from file
DONE: remove dependency on subprocess library
DONE: remove docking highlights when you start dragging a window
DONE: resizing horizontally on reader is jittery? the jittering gets worse the further through the chapter you are (probably some bullshit with floating point stuff). possible fix, don't do a simple division but instead manually count up to the maximum page under the user progress and just snap to the top of that page
DONE: rework indexing to add the root path to the index files and then relative paths to the collections in self.location instead of just the basename. also add a manga field `location`
DONE: rework storedfile to have relative paths to the library root
DONE: reworking the image renderer to use an InvisibleButton instead of a customized ImageButton has broken the dragging for the image inspect
DONE: strange segfault i think? appeared when loading file info for chapters during testing. can't recreate but that's probably where it lies. could be with the iffy usage of the std::optional non-atomically
DONE: strip (Digital) (1r0n) from the titles of manga
DONE: support cover.jpg items in archives
DONE: support errors for entire archives so we can short-circuit and not have 1000 errors for individual pages in the archive
DONE: support opening files in archive with prefixed folder names: "Chapter 01/004.jpg" and such
DONE: the very laggy lazy-loading of manga info is more of a harm than a boon, probably better to just have it load all at once but have a loading spinner as it loads instead of still trying to do the whole app mainloop
DONE: toggle fullscreen not working?
DONE: toggleable window long path prefix
DONE: try to remove the default title bar in the main viewport so they all look the same
DONE: unicode support in fonts
DONE: when resizing the reader, keep the scroll relative to the chapter. right now, resizing changes the size of the pages so if you make the window narrower, it will move you forward in the chapter
DONE: zip file error handling
DONE? opengl shader for filtering: either bicubic or lanczos
DONE? threaded queue for copy tasks so it isn't blocking (look at [glaze::pool](https://stephenberry.github.io/glaze/thread-pool/))
DONE? word-boundary and start-of-title filter similarity (sim("title", "title blah blah") > sim("title", "junktitlejunk"))
NOPE: add option for glaze json paths instead of jq filters
