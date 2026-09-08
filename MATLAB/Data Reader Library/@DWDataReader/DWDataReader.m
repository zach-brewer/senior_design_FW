classdef DWDataReader < handle
    properties (Access=public)
    end

    properties (SetAccess=protected)
        LibFile = "DWDataReaderLib64";
        HeaderFile = "DWDataReaderLibFuncs.h";
        LibIsLoaded = false;
        FileIsLoaded = false;
        status = "DWSTAT_OK";
        filepath = "None";
        DefaultSampleRate = NaN;
        UniqueSampleRates = NaN;
        EventCount = 0;
        ChannelCount = 0;
    end

    methods (Access=public)
        function DWReader = DWDataReader()
            DWReader.initialize()
        end

        ViewLibFunctions(DWReader)
        delete(DWReader)
        StatusCheck(DWReader)
        dw = ReadFile(DWReader, filepath)
    end

    methods (Access=protected)
        initialize(DWReader)
        EventList2 = GetEventList(DWReader)
        ChannelInfo = GetChannelInfo(DWReader)
        ChannelData2 = GetChannelData(DWReader, ChannelInfo, EventList)
        GlobalInfo = GetGlobalInfo(DWReader, ChannelInfo, EventList)
    end
end
