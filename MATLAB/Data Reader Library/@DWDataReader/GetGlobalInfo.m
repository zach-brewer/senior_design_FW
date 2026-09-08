function GlobalInfo = GetGlobalInfo(DWReader, ChannelInfo, EventList)
% Save miscellaneous global information.

GlobalInfo.ChannelCount = DWReader.ChannelCount; % excluding times vectors
GlobalInfo.TotalDuration = EventList.stop(end) - EventList.start(1); % [s]
GlobalInfo.TotalActiveDuration = sum(EventList.duration); % [s]
GlobalInfo.DefaultSampleRate = DWReader.DefaultSampleRate;
GlobalInfo.NChannelsAtDefaultSampleRate = sum(ChannelInfo.SampleRate == DWReader.DefaultSampleRate);
GlobalInfo.UniqueSampleRates = DWReader.UniqueSampleRates;
GlobalInfo.EventCount = DWReader.EventCount;
GlobalInfo.SeparateEventCount = sum(EventList.gap(2:end) ~= 0) + 1; % how many non-overlapping events are there
