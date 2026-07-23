file(READ "${SOURCE_FILE}" MAIN_SOURCE)

foreach(FORBIDDEN "v2.wttr.in" "refreshWeatherImage" ".png?lang=")
    string(FIND "${MAIN_SOURCE}" "${FORBIDDEN}" POSITION)
    if(NOT POSITION EQUAL -1)
        message(FATAL_ERROR
            "weather worker still contains forbidden PNG path: ${FORBIDDEN}")
    endif()
endforeach()
