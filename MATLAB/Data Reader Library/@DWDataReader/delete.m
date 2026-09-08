function delete(DWReader)
% Shutdown the Dewesoft data reader library.

% Deinitialize the library.
DWReader.status = calllib(DWReader.LibFile, "DWDeInit");
DWReader.StatusCheck()

% Unload the DLL from memory.
unloadlibrary(DWReader.LibFile);
DWReader.LibIsLoaded = false;
