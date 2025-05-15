#ifndef UTILS_H
#define UTILS_H

#include <WiFi.h>

#define WIFI_SSID "MHR"
#define WIFI_PASSWORD "12345678"

// Function to initialize WiFi
void initWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi-> SSID: ");
    Serial.print(WIFI_SSID);
    Serial.print(" Pass: ");
    Serial.print(WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }
    Serial.println(WiFi.localIP());
    Serial.println();
}



// Function to get the current datetime string
String getDateTimeString() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return "";
    }

    // Create a buffer to hold the formatted datetime string
    char timeString[25];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);

    // Return the datetime string as a String object
    return String(timeString);
}

String get12HourFormat(String datetime) {
    if (datetime.length() < 19) {  // Ensure valid datetime format "YYYY-MM-DD HH:MM:SS"
        return "Invalid datetime";
    }

    // Extract hours, minutes, and seconds
    String hoursStr = datetime.substring(11, 13);
    String minutes = datetime.substring(14, 16);
    String seconds = datetime.substring(17, 19);

    int hours = hoursStr.toInt();  // Convert string to integer
    String period = "AM";

    // Convert 24-hour format to 12-hour format
    if (hours == 0) {
        hours = 12;  // Midnight case
    } else if (hours == 12) {
        period = "PM";  // Noon case
    } else if (hours > 12) {
        hours -= 12;
        period = "PM";
    }

    // Construct the 12-hour formatted time string
    String time12Hour = String(hours) + ":" + minutes + ":" + seconds + " " + period;

    return time12Hour;
}

String getDateString(String datetime) {
    if (datetime.length() < 10) {  // Ensure valid datetime format "YYYY-MM-DD"
        return "Invalid datetime";
    }

    // Extract year, month, and day
    String year = datetime.substring(0, 4);
    String monthStr = datetime.substring(5, 7);
    String day = datetime.substring(8, 10);

    int month = monthStr.toInt();  // Convert month to integer

    // Array of month names
    String months[] = {"January", "February", "March", "April", "May", "June",
                       "July", "August", "September", "October", "November", "December"};

    // Validate month and return formatted string
    if (month >= 1 && month <= 12) {
        return day + " " + months[month - 1] + ", " + year;
    } else {
        return "Invalid month";
    }
}
#endif
