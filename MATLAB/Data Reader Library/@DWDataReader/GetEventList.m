function EventList2 = GetEventList(DWReader)
% Read each triggered event from the data file.

DWReader.LibFile = "DWDataReaderLib64";

% Get the event count
EventCount = calllib(DWReader.LibFile, "DWGetEventListCount");
if EventCount == 0
    error("There are no events, which likely means the data file is empty.")
end

% Initialize variables. Matlab implicitly converts these to the
% appropriate type/pointer before passing them as arguments to the
% library function.
EventType = 0;
EventTimestamp = 0;
MaxTextCharacters = 200;
EventTextBuffer = blanks(MaxTextCharacters);

% Read each event
for i = 1:EventCount
    % Get the event information and check the status of the call.
    EventIndex = i-1;
    [DWReader.status, EventType, EventTimestamp, EventText] = calllib(DWReader.LibFile, "DWGetEventListItem", EventIndex, EventType, EventTimestamp, EventTextBuffer, MaxTextCharacters);
    DWReader.StatusCheck()

    % The event type must be the start or end of data recording, or a
    % trigger event.
    if EventType > 3
        error("Allowed event types: 1, 2, 3\nCurrent event type: %i", EventType)
    end

    % Save the returned information into a structure array.
    EventList1(i,1).EventType = EventType;
    EventList1(i,1).EventTimestamp = EventTimestamp;
    EventList1(i,1).EventText = string(EventText);
end

% Convert the event list structure array to a table.
EventList1 = struct2table(EventList1, AsArray=true);

% Reformat EventList1 by grouping events by type.
EventList2 = table;
EventList2.index = [1 : EventCount/3]';
EventList2.start = EventList1.EventTimestamp(EventList1.EventType == 1);
EventList2.stop = EventList1.EventTimestamp(EventList1.EventType == 2);
EventList2.duration = EventList2.stop - EventList2.start;
EventList2.trigger = EventList1.EventTimestamp(EventList1.EventType == 3);
EventList2.gap = [NaN; EventList2.start(2:end) - EventList2.stop(1:end-1)];

% Store the event count as a property of the class.
DWReader.EventCount = EventCount / 3;
