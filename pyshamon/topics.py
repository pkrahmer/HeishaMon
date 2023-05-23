import logging

import decode
import descriptions
from datetime import datetime


def checksum(data: []) -> int:
    chk = 0
    for b in data:
        chk += b
    chk = (chk ^ 0xFF) + 0x01
    return chk & 0xFF


def valid_checksum(data: []) -> bool:
    return checksum(data[:-1]) == data[-1]


class Topic:
    def __init__(self, name: str, textual_description: any, fnc: any, topic_type: str = "main"):
        self.name = name
        self.textual_description = textual_description
        self.fnc = fnc
        self.description = None
        self.value = None
        self.since = None
        self.type = topic_type
        self.delegated: bool = True
        pass

    def __str__(self):
        return f'{self.name} = {self.value} {self.description}'

    def update_description(self):
        if self.textual_description is None or len(self.textual_description) == 0:
            self.description = None
        elif len(self.textual_description) == 1:
            self.description = self.textual_description[0]
        elif self.value is None:
            self.description = None
        elif int(self.value) >= len(self.textual_description):
            self.description = "unknown"
        else:
            self.description = self.textual_description[int(self.value)]
        self.delegated = False

    def update(self, packet_data: bytearray):
        new_value = self.fnc(packet_data)
        if new_value != self.value:
            self.value = new_value
            self.since = datetime.now()
            self.update_description()
        pass

    def changed_since(self, since: datetime):
        return self.since is not None and (since is None or since < self.since)


class Topics:
    def __init__(self):
        self.topics = [
            Topic("Heatpump_State", descriptions.OffOn, lambda d: decode.bits_7_and_8(d[4])),
            Topic("Pump_Flow", descriptions.LitersPerMin, lambda d: decode.get_pump_flow(d)),
            Topic("Force_DHW_State", descriptions.DisabledEnabled, lambda d: decode.bits_1_and_2(d[4])),
            Topic("Quiet_Mode_Schedule", descriptions.DisabledEnabled, lambda d: decode.bits_1_and_2(d[7])),
            Topic("Operating_Mode_State", descriptions.OpModeDesc, lambda d: decode.get_op_mode(d[6])),
            Topic("Main_Inlet_Temp", descriptions.Celsius, lambda d: decode.get_inlet_temp(d)),
            Topic("Main_Outlet_Temp", descriptions.Celsius, lambda d: decode.get_outlet_temp(d)),
            Topic("Main_Target_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[153])),
            Topic("Compressor_Freq", descriptions.Hertz, lambda d: decode.int_minus_1(d[166])),
            Topic("DHW_Target_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[42])),
            Topic("DHW_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[141])),
            Topic("Operations_Hours", descriptions.Hours, lambda d: decode.word(d[183], d[182]) - 1),
            Topic("Operations_Counter", descriptions.Counter, lambda d: decode.word(d[180], d[179]) - 1),
            Topic("Main_Schedule_State", descriptions.DisabledEnabled, lambda d: decode.bits_1_and_2(d[5])),
            Topic("Outside_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[142])),
            Topic("Heat_Energy_Production", descriptions.Watt, lambda d: decode.get_energy(d[194])),
            Topic("Heat_Energy_Consumption", descriptions.Watt, lambda d: decode.get_energy(d[193])),
            Topic("Powerful_Mode_Time", descriptions.Powerfulmode, lambda d: decode.right_3_bits(d[7])),
            Topic("Quiet_Mode_Level", descriptions.Quietmode, lambda d: decode.bits_3_to_5(d[7])),
            Topic("Holiday_Mode_State", descriptions.HolidayState, lambda d: decode.bits_3_and_4(d[5])),
            Topic("ThreeWay_Valve_State", descriptions.Valve, lambda d: decode.bits_7_and_8(d[111])),
            Topic("Outside_Pipe_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[158])),
            Topic("DHW_Heat_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[99])),
            Topic("Heat_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[84])),
            Topic("Cool_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[94])),
            Topic("DHW_Holiday_Shift_Temp", descriptions.Kelvin, lambda d: decode.int_minus_128(d[44])),
            Topic("Defrosting_State", descriptions.DisabledEnabled, lambda d: decode.bits_5_and_6(d[111])),
            Topic("Z1_Heat_Request_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[38])),
            Topic("Z1_Cool_Request_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[39])),
            Topic("Z1_Heat_Curve_Target_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[75])),
            Topic("Z1_Heat_Curve_Target_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[76])),
            Topic("Z1_Heat_Curve_Outside_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[78])),
            Topic("Z1_Heat_Curve_Outside_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[77])),
            Topic("Room_Thermostat_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[156])),
            Topic("Z2_Heat_Request_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[40])),
            Topic("Z2_Cool_Request_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[41])),
            Topic("Z1_Water_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[145])),
            Topic("Z2_Water_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[146])),
            Topic("Cool_Energy_Production", descriptions.Watt, lambda d: decode.get_energy(d[196])),
            Topic("Cool_Energy_Consumption", descriptions.Watt, lambda d: decode.get_energy(d[195])),
            Topic("DHW_Energy_Production", descriptions.Watt, lambda d: decode.get_energy(d[198])),
            Topic("DHW_Energy_Consumption", descriptions.Watt, lambda d: decode.get_energy(d[197])),
            Topic("Z1_Water_Target_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[147])),
            Topic("Z2_Water_Target_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[148])),
            Topic("Error", descriptions.ErrorState, lambda d: decode.get_error_info(d)),
            Topic("Room_Holiday_Shift_Temp", descriptions.Kelvin, lambda d: decode.int_minus_128(d[43])),
            Topic("Buffer_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[149])),
            Topic("Solar_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[150])),
            Topic("Pool_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[151])),
            Topic("Main_Hex_Outlet_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[154])),
            Topic("Discharge_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[155])),
            Topic("Inside_Pipe_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[157])),
            Topic("Defrost_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[159])),
            Topic("Eva_Outlet_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[160])),
            Topic("Bypass_Outlet_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[161])),
            Topic("Ipm_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[162])),
            Topic("Z1_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[139])),
            Topic("Z2_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[140])),
            Topic("DHW_Heater_State", descriptions.BlockedFree, lambda d: decode.bits_5_and_6(d[9])),
            Topic("Room_Heater_State", descriptions.BlockedFree, lambda d: decode.bits_7_and_8(d[9])),
            Topic("Internal_Heater_State", descriptions.InactiveActive, lambda d: decode.bits_7_and_8(d[112])),
            Topic("External_Heater_State", descriptions.InactiveActive, lambda d: decode.bits_5_and_6(d[112])),
            Topic("Fan1_Motor_Speed", descriptions.RotationsPerMin, lambda d: decode.int_minus_1_times_10(d[173])),
            Topic("Fan2_Motor_Speed", descriptions.RotationsPerMin, lambda d: decode.int_minus_1_times_10(d[174])),
            Topic("High_Pressure", descriptions.Pressure, lambda d: decode.int_minus_1_div_5(d[163])),
            Topic("Pump_Speed", descriptions.RotationsPerMin, lambda d: decode.int_minus_1_times_50(d[171])),
            Topic("Low_Pressure", descriptions.Pressure, lambda d: decode.int_minus_1(d[164])),
            Topic("Compressor_Current", descriptions.Ampere, lambda d: decode.int_minus_1_div_5(d[165])),
            Topic("Force_Heater_State", descriptions.InactiveActive, lambda d: decode.bits_5_and_6(d[5])),
            Topic("Sterilization_State", descriptions.InactiveActive, lambda d: decode.bits_5_and_6(d[117])),
            Topic("Sterilization_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[100])),
            Topic("Sterilization_Max_Time", descriptions.Minutes, lambda d: decode.int_minus_1(d[101])),
            Topic("Z1_Cool_Curve_Target_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[86])),
            Topic("Z1_Cool_Curve_Target_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[87])),
            Topic("Z1_Cool_Curve_Outside_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[89])),
            Topic("Z1_Cool_Curve_Outside_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[88])),
            Topic("Heating_Mode", descriptions.HeatCoolModeDesc, lambda d: decode.bits_7_and_8(d[28])),
            Topic("Heating_Off_Outdoor_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[83])),
            Topic("Heater_On_Outdoor_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[85])),
            Topic("Heat_To_Cool_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[95])),
            Topic("Cool_To_Heat_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[96])),
            Topic("Cooling_Mode", descriptions.HeatCoolModeDesc, lambda d: decode.bits_5_and_6(d[28])),
            Topic("Z2_Heat_Curve_Target_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[79])),
            Topic("Z2_Heat_Curve_Target_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[80])),
            Topic("Z2_Heat_Curve_Outside_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[82])),
            Topic("Z2_Heat_Curve_Outside_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[81])),
            Topic("Z2_Cool_Curve_Target_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[90])),
            Topic("Z2_Cool_Curve_Target_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[91])),
            Topic("Z2_Cool_Curve_Outside_High_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[93])),
            Topic("Z2_Cool_Curve_Outside_Low_Temp", descriptions.Celsius, lambda d: decode.int_minus_128(d[92])),
            Topic("Room_Heater_Operations_Hours", descriptions.Hours, lambda d: decode.word(d[186], d[185]) - 1),
            Topic("DHW_Heater_Operations_Hours", descriptions.Hours, lambda d: decode.word(d[189], d[188]) - 1),
            Topic("Heat_Pump_Model", descriptions.Model, lambda d: decode.get_model(d)),
            Topic("Pump_Duty", descriptions.Duty, lambda d: decode.int_minus_1(d[172])),
            Topic("Zones_State", descriptions.ZonesState, lambda d: decode.bits_1_and_2(d[6])),
            Topic("Max_Pump_Duty", descriptions.Duty, lambda d: decode.int_minus_1(d[45])),
            Topic("Heater_Delay_Time", descriptions.Minutes, lambda d: decode.int_minus_1(d[104])),
            Topic("Heater_Start_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[105])),
            Topic("Heater_Stop_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[106])),
            Topic("Buffer_Installed", descriptions.DisabledEnabled, lambda d: decode.bits_5_and_6(d[24])),
            Topic("DHW_Installed", descriptions.DisabledEnabled, lambda d: decode.bits_7_and_8(d[24])),
            Topic("Solar_Mode", descriptions.SolarModeDesc, lambda d: decode.bits_3_and_4(d[24])),
            Topic("Solar_On_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[61])),
            Topic("Solar_Off_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[62])),
            Topic("Solar_Frost_Protection", descriptions.Celsius, lambda d: decode.int_minus_128(d[63])),
            Topic("Solar_High_Limit", descriptions.Celsius, lambda d: decode.int_minus_128(d[64])),
            Topic("Pump_Flowrate_Mode", descriptions.PumpFlowRateMode, lambda d: decode.bits_3_and_4(d[29])),
            Topic("Liquid_Type", descriptions.LiquidType, lambda d: decode.bit_1(d[20])),
            Topic("Alt_External_Sensor", descriptions.DisabledEnabled, lambda d: decode.bits_3_and_4(d[20])),
            Topic("Anti_Freeze_Mode", descriptions.DisabledEnabled, lambda d: decode.bits_5_and_6(d[20])),
            Topic("Optional_PCB", descriptions.DisabledEnabled, lambda d: decode.bits_7_and_8(d[20])),
            Topic("Z1_Sensor_Settings", descriptions.ZonesSensorType, lambda d: decode.get_second_byte(d[22])),
            Topic("Z2_Sensor_Settings", descriptions.ZonesSensorType, lambda d: decode.get_first_byte(d[22])),
            Topic("Buffer_Tank_Delta", descriptions.Kelvin, lambda d: decode.int_minus_128(d[59])),
            Topic("External_Pad_Heater", descriptions.ExtPadHeaterType, lambda d: decode.bits_3_and_4(d[25])),

            Topic("Z1_Water_Pump", descriptions.OffOn, lambda d: d[4] >> 7, "optional"),
            Topic("Z1_Mixing_Valve", descriptions.MixingValve, lambda d: (d[4] >> 5) & 0b11, "optional"),
            Topic("Z2_Water_Pump", descriptions.OffOn, lambda d: (d[4] >> 4) & 0b1, "optional"),
            Topic("Z2_Mixing_Valve", descriptions.MixingValve, lambda d: (d[4] >> 2) & 0b11, "optional"),
            Topic("Pool_Water_Pump", descriptions.OffOn, lambda d: (d[4] >> 1) & 0b1, "optional"),
            Topic("Solar_Water_Pump", descriptions.OffOn, lambda d: (d[4] >> 0) & 0b1, "optional"),
            Topic("Alarm_State", descriptions.OffOn, lambda d: (d[5] >> 0) & 0b1, "optional")
        ]

    def decode_and_update(self, data: []) -> bool:
        if not len(data) in [20, 203]:
            return False

        if not valid_checksum(data):
            logging.info("topics: invalid checksum received")
            return False

        if len(data) == 203:
            for topic in self.topics:
                if topic.type == "main":
                    topic.update(data)
            return True

        if len(data) == 20:
            for topic in self.topics:
                if topic.type == "optional":
                    topic.update(data)
            return True

        return False

    def print(self, since: datetime) -> None:
        for topic in self.topics:
            if topic.changed_since(since):
                print(topic)

#for Topics().decode_and_update([113, 200, 1, 16, 85, 85, 97, 97, 0, 85, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 90, 21, 17, 85, 86, 29, 85, 90, 89, 26, 0, 0, 0, 0, 0, 0, 0, 0, 128, 135, 128, 138, 170, 113, 113, 113, 153, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 133, 21, 138, 133, 133, 208, 123, 120, 31, 126, 31, 31, 111, 51, 121, 51, 161, 158, 113, 138, 183, 163, 123, 143, 149, 133, 120, 135, 138, 148, 158, 138, 138, 148, 158, 130, 143, 138, 17, 91, 126, 183, 11, 128, 128, 31, 120, 122, 0, 0, 0, 149, 85, 85, 33, 133, 21, 85, 5, 9, 18, 105, 0, 0, 0, 0, 0, 0, 0, 0, 194, 211, 11, 51, 101, 178, 211, 11, 148, 101, 143, 0, 175, 153, 147, 143, 50, 50, 158, 163, 50, 50, 50, 128, 158, 147, 160, 152, 147, 154, 97, 155, 97, 156, 80, 1, 1, 1, 0, 0, 34, 0, 1, 1, 1, 1, 121, 121, 1, 1, 76, 10, 0, 95, 8, 0, 29, 0, 0, 1, 0, 0, 6, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 5])