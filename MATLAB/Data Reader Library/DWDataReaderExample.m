clear, clc, close all

% File location
ParentDir = "D:\OneDrive - Colorado School of Mines\UMPIRE Projects\Data\Human Evaluation Data\2025-05-31 Human Evals Day 1 at Elbert";
WorkingDir = "19_Gerardo_M240-01_Unbraced";
filename = "Dewesoft Data.dxd";
filepath = fullfile(ParentDir, WorkingDir, filename);

% Read the file
DWReader = DWDataReader();
dw = DWReader.ReadFile(filepath)

% Extract data.
NBursts = dw.global.EventCount
ChannelName = "LC3";
ChannelIndex = find(dw.channels.name == ChannelName);
if length(ChannelIndex) > 1
    error("There are multiple channels with the same name.")
end
TimeVectorName = dw.channels.TimeVectorName(ChannelIndex);

% Plot data.
fg = StdFig;
ax = StdAxes;
ax.ColorOrder = InterpolateColors('CustomColorOrder1', NBursts);
for i = 1:NBursts
    t = dw.data(i).(TimeVectorName);
    y = dw.data(i).(ChannelName);
    plot(t, y)
end
