function ChannelData2 = GetChannelData(DWReader, ChannelInfo, EventList)
% This is a helper function for DWDataReaderMPA_ReadFile().

% Read data for each channel. The library returns the data across all
% events in one concatenated vector.
TimeVectors = struct();
for i = 1:DWReader.ChannelCount
    % Is there actually any data within the channel?
    if ChannelInfo.SampleCount(i) == 0
        fprintf(2, 'Channel %s has no data.\n', ChannelInfo.Name(i))
        continue
    end

    % Initialize variables. Matlab implicitly converts these to the
    % appropriate type/pointer before passing them as arguments to the
    % library function.
    data = zeros(ChannelInfo.SampleCount(i), 1, "double");
    time = zeros(ChannelInfo.SampleCount(i), 1, "double");

    % Read the channel data
    ChannelIndex = i-1;
    OffsetPosition = 0; % Use if you want to read from a specific position within the array.
    [DWReader.status, data, time] = calllib(DWReader.LibFile, "DWGetScaledSamples", ChannelIndex, OffsetPosition, ChannelInfo.SampleCount(i), data, time);
    DWReader.StatusCheck()

    % Save the channel data to a structure.
    ChannelData1.(ChannelInfo.name(i)) = data;

    % Store the timestamps data if it does not already exist.
    if ~isfield(TimeVectors, ChannelInfo.TimeVectorName(i))
        TimeVectors.(ChannelInfo.TimeVectorName(i)) = time;
    end
end

% Reorder the timestamp vectors structure to match the order of the unique
% sample rates vector.
TimeVectorNames = string(fieldnames(TimeVectors));
if length(TimeVectorNames) > 1
    [~, SortOrder] = sort(TimeVectorNames, "descend");
    SortOrder = [SortOrder(end); SortOrder(1:end-1)];
    TimeVectors = orderfields(TimeVectors, SortOrder);
    TimeVectorNames = TimeVectorNames(SortOrder);
end

% Find indices for the start and end of each triggered data recording event
% and for each unique sample rate. The event timestamps themselves are 
% independent of the sample rate. However, there will be a different set of
% indices for each unique sample rate, since the vectors will be different
% lengths. Also note that the start of a recording duration is inclusive of 
% the first sample, while the end of a recording duration is an open
% boundary. The last recorded sample is actually one sampling period 
% previous to the timestamp that marks the end of the recording duration.
for i = 1:DWReader.EventCount
    for j = 1:length(DWReader.UniqueSampleRates)
        time = TimeVectors.(TimeVectorNames(j));
        EventIndices(i,1).(TimeVectorNames(j)).start = find(time >= EventList.start(i), 1, 'first');
        EventIndices(i,1).(TimeVectorNames(j)).stop = find(time < EventList.stop(i), 1, 'last');
    end
end

% Restructure data by triggered event.
for i = 1:DWReader.EventCount
    for j = 1:DWReader.ChannelCount
        k1 = EventIndices(i).(ChannelInfo.TimeVectorName(j)).start;
        k2 = EventIndices(i).(ChannelInfo.TimeVectorName(j)).stop;
        ChannelData2(i,1).(ChannelInfo.name(j)) = ChannelData1.(ChannelInfo.name(j))(k1:k2);
    end

    for j = 1:length(DWReader.UniqueSampleRates)
        k1 = EventIndices(i).(TimeVectorNames(j)).start;
        k2 = EventIndices(i).(TimeVectorNames(j)).stop;
        ChannelData2(i,1).(TimeVectorNames(j)) = TimeVectors.(TimeVectorNames(j))(k1:k2);
    end
end
