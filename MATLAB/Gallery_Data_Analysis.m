close all

addpath('C:\Users\Antwaan\Colorado School of Mines\GRP-Senior Design - Electronic Downrange Target Systems - General\Matlab\Data Reader Library');


% File location
ParentDir = "C:\Users\Antwaan\Colorado School of Mines\GRP-Senior Design - Electronic Downrange Target Systems - General\Dewesoft";
WorkingDir = "2026-09-02 The Gallery";
filename = "Pistol_9mm_SingleShot_25ft_Test2.dxd";
filepath = fullfile(ParentDir, WorkingDir, filename);

% Read the file
DWReader = DWDataReader();
dw = DWReader.ReadFile(filepath);

% Extract data
threshold = .5;
NEvents = dw.global.EventCount;
for i = 1:NEvents
    t = dw.data(i).t;
    mic1 = dw.data(i).mic1;
    mic2 = dw.data(i).mic2;
    mic3 = dw.data(i).mic3;
    mic1Filter = dw.data(i).mic1Filter;
    mic2Filter = dw.data(i).mic2Filter;
    mic3Filter = dw.data(i).mic3Filter;

    figure;
    subplot(3,1,1);
    plot(t, mic1, t, mic1Filter);
    title('Mic 1');
    subplot(3,1,2);
    plot(t, mic2, t, mic2Filter);
    title('Mic 2');
    subplot(3,1,3);
    plot(t, mic3, t, mic3Filter);
    title('Mic 3');

    idx = find(mic1 >= threshold, 1, 'first');
    timeCross = t(idx)
end