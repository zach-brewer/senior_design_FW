function initialize(DWReader)
% Initialize the Dewesoft data reader library.
%
% If one of the arguments to any of the DLL functions is a pointer, Matlab
% dereferences the pointer, and then appends the updated value as 
% additional output arguments. Note that in C, all strings are effectively 
% pointers and have the same behavior as described.

% If the DLL is already open, properly close it down.
if libisloaded(DWReader.LibFile)
    fprintf("DLL was left open. Closing now.\n");
    DWReader.status = calllib(DWReader.LibFile, "DWCloseDataFile");
    DWReader.status = calllib(DWReader.LibFile, "DWDeInit");
    DWReader.StatusCheck()
    unloadlibrary(DWReader.LibFile)
    DWReader.LibIsLoaded = false;
end

% Load and initialize the DLL.
warning("off", "MATLAB:loadlibrary:cppoutput");
loadlibrary(DWReader.LibFile, DWReader.HeaderFile)
warning("on", "MATLAB:loadlibrary:cppoutput");
DWReader.status = calllib(DWReader.LibFile, "DWInit");
DWReader.StatusCheck()
DWReader.LibIsLoaded = true;
