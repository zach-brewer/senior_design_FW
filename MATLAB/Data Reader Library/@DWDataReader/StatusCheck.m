function StatusCheck(DWReader)
% DWStatus is an enum in DWDataReaderLib.h. MATLAB converts the enum to its
% string equivalent.

switch DWReader.status
    case "DWSTAT_OK"
        % Status OK, do nothing.

    case "DWSTAT_ERROR"
        error("DWSTAT_ERROR: error occurred in the dll")

    case "DWSTAT_ERROR_FILE_CANNOT_OPEN"
        error("DWSTAT_ERROR_FILE_CANNOT_OPEN: unable to open file")

    case "DWSTAT_ERROR_FILE_ALREADY_IN_USE"
        error("DWSTAT_ERROR_FILE_ALREADY_IN_USE: file already in use")

    case "DWSTAT_ERROR_FILE_CORRUPT"
        error("DWSTAT_ERROR_FILE_CORRUPT: file corrupted")

    case "DWSTAT_ERROR_NO_MEMORY_ALLOC"
        error("DWSTAT_ERROR_NO_MEMORY_ALLOC: memory not allocated")

    case "DWSTAT_ERROR_CREATE_DEST_FILE"
        error("DWSTAT_ERROR_CREATE_DEST_FILE: error creating destination file (only for d7d, d7z, dxd, dxz files)")

    case "DWSTAT_ERROR_EXTRACTING_FILE"
        error("DWSTAT_ERROR_EXTRACTING_FILE: error extracting data (only for d7d, d7z, dxd, dxz files)")

    case "DWSTAT_ERROR_CANNOT_OPEN_EXTRACTED_FILE"
        error("DWSTAT_ERROR_CANNOT_OPEN_EXTRACTED_FILE: error opening extracted file (only for d7d, d7z, dxd, dxz files)")

    otherwise
        error("Unknown status occurred.\nDWStatus was: %s", DWReader.status)
end
