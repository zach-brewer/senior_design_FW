function dw = ReadFile(DWReader, filepath)
% Read data from a dewesoft file.

% Open dewesoft data file.
DWReader.filepath = filepath;
[DWReader.status, ~, FileInfo] = calllib(DWReader.LibFile, "DWOpenDataFile", char(filepath), struct());
DWReader.StatusCheck()
DWReader.FileIsLoaded = true;
DWReader.DefaultSampleRate = FileInfo.sample_rate;

% Get the event list, channel info, channel data, and global info.
dw.events = DWReader.GetEventList();
dw.channels = DWReader.GetChannelInfo();
dw.data = DWReader.GetChannelData(dw.channels, dw.events);
dw.global = GetGlobalInfo(DWReader, dw.channels, dw.events);

% Close dewesoft data file.
DWReader.status = calllib(DWReader.LibFile, "DWCloseDataFile");
DWReader.StatusCheck()
DWReader.FileIsLoaded = false;
