function ChannelInfo = GetChannelInfo(DWReader)
% Retrieve metadata for each channel.

DefaultSampleRate = DWReader.DefaultSampleRate;

% Get the channel count. This does not include timestamp vectors.
ChannelCount = calllib(DWReader.LibFile, "DWGetChannelListCount");
if ChannelCount == -1
    error("DWGetChannelListCount returns -1 if there was an error.");
elseif ChannelCount == 0
    error("There are no available channels.")
end

for i = 1:ChannelCount
    ChannelIndex = i-1;

    % Get the size of the XML properties block.
    ChannelPropertiesSelector = 8; % DW_CH_XML_LEN
    XMLSize = zeros(1, 1, "int32");
    BufferLength = 4; % [bytes]
    [DWReader.status, XMLSize, ~] = calllib(DWReader.LibFile, "DWGetChannelProps", ChannelIndex, ChannelPropertiesSelector, XMLSize, BufferLength);
    DWReader.StatusCheck()

    % Get the actual XML data string.
    ChannelPropertiesSelector = 7; % DW_CH_XML
    XMLData = zeros(1, XMLSize, "int8");
    BufferLength = XMLSize; % [bytes]
    [DWReader.status, XMLData, ~] = calllib(DWReader.LibFile, "DWGetChannelProps", ChannelIndex, ChannelPropertiesSelector, XMLData, BufferLength);
    DWReader.StatusCheck()
    XMLData = string(char(XMLData));

    % Extract basic channel properties from the XML data.
    ChannelInfo(i,1).name = replace(extractBetween(XMLData, "<Name>", "</Name>"), " ", "_");
    ChannelInfo(i,1).description = extractBetween(XMLData, "<Description>", "</Description>");
    ChannelInfo(i,1).PhysicalQuantity = extractBetween(XMLData, "<PhysicalQuantity>", "</PhysicalQuantity>");
    ChannelInfo(i,1).unit = extractBetween(XMLData, "<Unit>", "</Unit>");
    ChannelInfo(i,1).color = extractBetween(XMLData, "<DisplayColor>", "</DisplayColor>");

    % Extract the sample rate and the sample count.
    ChannelInfo(i,1).SampleRate = DefaultSampleRate / double(extractBetween(XMLData, "<SRDiv" + wildcardPattern + ">", "</SRDiv>"));
    ChannelInfo(i,1).SampleCount = calllib(DWReader.LibFile, "DWGetScaledSamplesCount", ChannelIndex);

    % Is the channel sample rate the same as the default sample rate?
    if ChannelInfo(i,1).SampleRate == DefaultSampleRate
        ChannelInfo(i,1).SampleRateIsDefault = true;
    else
        ChannelInfo(i,1).SampleRateIsDefault = false;
    end

    % The name of the timestamps vector depends on the sample rate.
    if ChannelInfo(i,1).SampleRate == DefaultSampleRate
        ChannelInfo(i,1).TimeVectorName = "t";
    else
        ChannelInfo(i,1).TimeVectorName = sprintf("t_%i", ChannelInfo(i,1).SampleRate);
    end

    % Calibration scaling used on the raw voltage inputs.
    ChannelInfo(i,1).CalibrationScale = double(extractBetween(extractBefore(XMLData, "<SIUnit>"), "<Scale>", "</Scale>"));
    if isempty(ChannelInfo(i,1).CalibrationScale)
        ChannelInfo(i,1).CalibrationScale = 1.0;
    end

    % Minimum and maximum possible signal values given the configured range
    % of the amplifier.
    ChannelInfo(i,1).RangeMin = double(extractBetween(XMLData, "<RangeMin>", "</RangeMin>"));
    ChannelInfo(i,1).RangeMax = double(extractBetween(XMLData, "<RangeMax>", "</RangeMax>"));

    % Provided sensor serial number.
    ChannelInfo(i,1).SensorSN = extractBetween(XMLData, "<Sensor>", "</Sensor>");
    if isempty(ChannelInfo(i,1).SensorSN)
        ChannelInfo(i,1).SensorSN = "N/A";
    end
end
ChannelInfo = struct2table(ChannelInfo, AsArray=true);

% Store the channel count and unique sample rates as properties of the class.
DWReader.ChannelCount = ChannelCount;
DWReader.UniqueSampleRates = sort(unique(ChannelInfo.SampleRate), "descend");
