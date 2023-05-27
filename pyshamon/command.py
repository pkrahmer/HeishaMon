from topics import checksum

panasonicPollQuery = [0x71, 0x6c, 0x01, 0x10] + [0x00]*106
panasonicSendQuery = [0xf1, 0x6c, 0x01, 0x10] + [0x00]*106
optionalPCBQuery = [0xF1, 0x11, 0x01, 0x50, 0x00, 0x00, 0x40, 0xFF, 0xFF, 0xE5,
                    0xFF, 0xFF, 0x00, 0xFF, 0xEB, 0xFF, 0xFF, 0x00, 0x00]


class Command:

    def __init__(self):
        self.send_query = panasonicSendQuery.copy()
        self.optional_query = optionalPCBQuery.copy()
        self.commands = {
            # set heatpump state to on by sending 1
            "SetHeatpump": self.set_heatpump_state,
            # set pump state to on by sending 1
            "SetPump": self.set_pump_service_mode,
            # set max pump duty
            "SetMaxPumpDuty": self.set_max_pump_duty,
            # set 0 for Off mode, set 1 for Quiet mode 1, set 2 for Quiet mode 2, set 3 for Quiet mode 3
            "SetQuietMode": self.set_quiet_mode,
            # z1 heat request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
            "SetZ1HeatRequestTemperature": self.set_z1_heat_request_temperature,
            # z1 cool request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
            "SetZ1CoolRequestTemperature": self.set_z1_cool_request_temperature,
            # z2 heat request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
            "SetZ2HeatRequestTemperature": self.set_z2_heat_request_temperature,
            # z2 cool request temp -  set from -5 to 5 to get same temperature shift point or set direct temp
            "SetZ2CoolRequestTemperature": self.set_z2_cool_request_temperature,
            # set mode to force DHW by sending 1
            "SetForceDHW": self.set_force_dhw,
            # set mode to force defrost  by sending 1
            "SetForceDefrost": self.set_force_defrost,
            # set mode to force sterilization by sending 1
            "SetForceSterilization": self.set_force_sterilization,
            # set Holiday mode by sending 1, off will be 0
            "SetHolidayMode": self.set_holiday_mode,
            # set Powerful mode by sending 0 = off, 1 for 30min, 2 for 60min, 3 for 90 min
            "SetPowerfulMode": self.set_powerful_mode,
            # set Heat pump operation mode  3 = DHW only, 0 = heat only, 1 = cool only,
            # 2 = Auto, 4 = Heat+DHW, 5 = Cool+DHW, 6 = Auto + DHW
            "SetOperationMode": self.set_operation_mode,
            # set DHW temperature by sending desired temperature between 40C-75C
            "SetDHWTemp": self.set_dhw_temp,
            # set heat/cool curves on z1 and z2
            "SetZ1HeatCurveTargetHighTemp": self.set_z1_heat_curve_target_high_temp,
            "SetZ1HeatCurveTargetLowTemp": self.set_z1_heat_curve_target_low_temp,
            "SetZ1HeatCurveOutsideHighTemp": self.set_z1_heat_curve_outside_high_temp,
            "SetZ1HeatCurveOutsideLowTemp": self.set_z1_heat_curve_outside_low_temp,
            "SetZ2HeatCurveTargetHighTemp": self.set_z2_heat_curve_target_high_temp,
            "SetZ2HeatCurveTargetLowTemp": self.set_z2_heat_curve_target_low_temp,
            "SetZ2HeatCurveOutsideHighTemp": self.set_z2_heat_curve_outside_high_temp,
            "SetZ2HeatCurveOutsideLowTemp": self.set_z2_heat_curve_outside_low_temp,
            "SetZ1CoolCurveTargetHighTemp": self.set_z1_cool_curve_target_high_temp,
            "SetZ1CoolCurveTargetLowTemp": self.set_z1_cool_curve_target_low_temp,
            "SetZ1CoolCurveOutsideHighTemp": self.set_z1_cool_curve_outside_high_temp,
            "SetZ1CoolCurveOutsideLowTemp": self.set_z1_cool_curve_outside_low_temp,
            "SetZ2CoolCurveTargetHighTemp": self.set_z2_cool_curve_target_high_temp,
            "SetZ2CoolCurveTargetLowTemp": self.set_z2_cool_curve_target_low_temp,
            "SetZ2CoolCurveOutsideHighTemp": self.set_z2_cool_curve_outside_high_temp,
            "SetZ2CoolCurveOutsideLowTemp": self.set_z2_cool_curve_outside_low_temp,
            # set zones to active
            "SetZones": self.set_zones,
            "SetFloorHeatDelta": self.set_floor_heat_delta,
            "SetFloorCoolDelta": self.set_floor_cool_delta,
            "SetDHWHeatDelta": self.set_dhw_heat_delta,
            "SetReset": self.set_reset,
            "SetHeaterDelayTime": self.set_heater_delay_time,
            "SetHeaterStartDelta": self.set_heater_start_delta,
            "SetHeaterStopDelta": self.set_heater_stop_delta,
            "SetMainSchedule": self.set_main_schedule,
            "SetAltExternalSensor": self.set_alt_external_sensor,
            "SetExternalPadHeater": self.set_external_pad_heater,
            "SetBufferDelta": self.set_buffer_delta,
        }

    def command_query(self) -> []:
        return self.send_query + [checksum(self.send_query)]

    def poll_query(self) -> []:
        return panasonicPollQuery.copy() + [checksum(panasonicPollQuery)]

    def known_commands(self) -> []:
        return list(self.commands)

    def reset(self):
        self.send_query = panasonicSendQuery.copy()

    def set(self, command: str, value: int) -> bool:
        for name, fnc in self.commands.items():
            if name.lower() == command.lower():
                fnc(value)
                return True

        return False

    def set_command(self, index: int, byte: int):
        self.send_query[index] = byte & 0xFF

    def set_heatpump_state(self, onoff: int):
        self.set_command(4, 2 if onoff else 1)

    def set_pump_service_mode(self, onoff: int):
        self.set_command(4, 32 if onoff else 16)

    def set_max_pump_duty(self, duty: int):
        self.set_command(45, duty + 1)

    def set_quiet_mode(self, mode: int):
        self.set_command(7, (min(3, max(0, mode))+1) * 8)

    def set_z1_heat_request_temperature(self, temperature: int):
        self.set_command(38, temperature + 128)

    def set_z1_cool_request_temperature(self, temperature: int):
        self.set_command(39, temperature + 128)

    def set_z2_heat_request_temperature(self, temperature: int):
        self.set_command(40, temperature + 128)

    def set_z2_cool_request_temperature(self, temperature: int):
        self.set_command(41, temperature + 128)

    def set_force_dhw(self, onoff: int):
        self.set_command(4, 128 if onoff else 64)

    def set_force_defrost(self, onoff: int):
        self.set_command(8, 2 if onoff else 0)

    def set_force_sterilization(self, onoff: int):
        self.set_command(8, 4 if onoff else 0)

    def set_holiday_mode(self, onoff: int):
        self.set_command(5, 32 if onoff else 16)

    def set_powerful_mode(self, mode: int):
        self.set_command(7, min(3, max(0, mode)) + 73)

    def set_dhw_temp(self, temperature: int):
        self.set_command(42, temperature + 128)

    def set_operation_mode(self, mode):
        self.set_command(6, [18, 19, 24, 33, 34, 35, 40][mode] if mode < 7 else 0)

    def set_zones(self, mode):
        self.set_command(6, [64, 128, 192][mode] if mode < 3 else 0)

    def set_floor_heat_delta(self, delta):
        self.set_command(84, delta + 128)

    def set_floor_cool_delta(self, delta):
        self.set_command(94, delta + 128)

    def set_dhw_heat_delta(self, delta):
        self.set_command(99, delta + 128)

    def set_reset(self, onoff: int):
        self.set_command(8, 1 if onoff else 0)

    def set_heater_delay_time(self, time):
        self.set_command(104, time + 1)

    def set_heater_start_delta(self, delta):
        self.set_command(105, delta + 128)

    def set_heater_stop_delta(self, delta):
        self.set_command(106, delta + 128)

    def set_main_schedule(self, onoff: int):
        self.set_command(5, 128 if onoff else 64)

    def set_alt_external_sensor(self, onoff: int):
        self.set_command(20, 32 if onoff else 16)

    def set_external_pad_heater(self, mode: int):
        self.set_command(25, 48 if mode == 2 else 32 if mode == 1 else 16)

    def set_buffer_delta(self, delta: int):
        self.set_command(59, delta + 128)

    def set_z1_heat_curve_target_high_temp(self, temp: int):
        self.set_command(75, temp + 128)

    def set_z1_heat_curve_target_low_temp(self, temp: int):
        self.set_command(76, temp + 128)

    def set_z1_heat_curve_outside_low_temp(self, temp: int):
        self.set_command(77, temp + 128)

    def set_z1_heat_curve_outside_high_temp(self, temp: int):
        self.set_command(78, temp + 128)

    def set_z2_heat_curve_target_high_temp(self, temp: int):
        self.set_command(79, temp + 128)

    def set_z2_heat_curve_target_low_temp(self, temp: int):
        self.set_command(80, temp + 128)

    def set_z2_heat_curve_outside_low_temp(self, temp: int):
        self.set_command(81, temp + 128)

    def set_z2_heat_curve_outside_high_temp(self, temp: int):
        self.set_command(82, temp + 128)

    def set_z1_cool_curve_target_high_temp(self, temp: int):
        self.set_command(86, temp + 128)

    def set_z1_cool_curve_target_low_temp(self, temp: int):
        self.set_command(87, temp + 128)

    def set_z1_cool_curve_outside_low_temp(self, temp: int):
        self.set_command(88, temp + 128)

    def set_z1_cool_curve_outside_high_temp(self, temp: int):
        self.set_command(89, temp + 128)

    def set_z2_cool_curve_target_high_temp(self, temp: int):
        self.set_command(90, temp + 128)

    def set_z2_cool_curve_target_low_temp(self, temp: int):
        self.set_command(91, temp + 128)

    def set_z2_cool_curve_outside_low_temp(self, temp: int):
        self.set_command(92, temp + 128)

    def set_z2_cool_curve_outside_high_temp(self, temp: int):
        self.set_command(93, temp + 128)


class OptionalCommand:

    def __init__(self):
        self.optional_query = optionalPCBQuery.copy()
        self.commands = {
            "SetHeatCoolMode": self.set_heat_cool_mode,
            "SetCompressorState": self.set_compressor_state,
            "SetSmartGridMode": self.set_smart_grid_mode,
            "SetExternalThermostat1State": self.set_external_thermostat_1_state,
            "SetExternalThermostat2State": self.set_external_thermostat_2_state,
            "SetDemandControl": self.set_demand_control,
            "SetPoolTemp": self.set_pool_temp,
            "SetBufferTemp": self.set_buffer_temp,
            "SetZ1RoomTemp": self.set_z1_room_temp,
            "SetZ1WaterTemp": self.set_z1_water_temp,
            "SetZ2RoomTemp": self.set_z2_room_temp,
            "SetZ2WaterTemp": self.set_z2_water_temp,
            "SetSolarTemp": self.set_solar_temp,
            "SetOptPCBByte9": self.set_byte_9
        }

    def optional_command_query(self) -> []:
        return self.optional_query + [checksum(self.optional_query)]

    def known_commands(self) -> []:
        return list(self.commands)

    def reset(self):
        self.optional_query = optionalPCBQuery.copy()

    def set(self, command: str, value: int) -> bool:
        for name, fnc in self.commands.items():
            if name.lower() == command.lower():
                fnc(value)
                return True

        return False

    def set_byte_6(self, val, base, bit):
        hex_val = (self.optional_query[6] & ~(base << bit)) | (val << bit)
        self.optional_query[6] = hex_val
        return len(self.optional_query)

    def set_byte_9(self, set_pcb_value):
        self.optional_query[9] = set_pcb_value

    def set_heat_cool_mode(self, set_pcb_value: int):
        self.set_byte_6(1 if set_pcb_value == 1 else 0, 0b1, 7)

    def set_compressor_state(self, set_pcb_value: int):
        self.set_byte_6(1 if set_pcb_value == 1 else 0, 0b1, 6)

    def set_smart_grid_mode(self, set_pcb_value: int):
        self.set_byte_6(0 if set_pcb_value < 0 else 3 if set_pcb_value > 3 else set_pcb_value, 0b11, 4)

    def set_external_thermostat_1_state(self, set_pcb_value: int):
        self.set_byte_6(0 if set_pcb_value < 0 else 3 if set_pcb_value > 3 else set_pcb_value, 0b11, 2)

    def set_external_thermostat_2_state(self, set_pcb_value: int):
        self.set_byte_6(0 if set_pcb_value < 0 else 3 if set_pcb_value > 3 else set_pcb_value, 0b11, 0)

    def set_demand_control(self, set_pcb_value: int):
        self.optional_query[14] = set_pcb_value

    def set_xxx_temp(self, temp: int, byte):
        self.optional_query[byte] = temp

    def set_pool_temp(self, temp: int):
        self.set_xxx_temp(temp, 7)

    def set_buffer_temp(self, temp: int):
        self.set_xxx_temp(temp, 8)

    def set_z1_room_temp(self, temp: int):
        self.set_xxx_temp(temp, 10)

    def set_z1_water_temp(self, temp: int):
        self.set_xxx_temp(temp, 16)

    def set_z2_room_temp(self, temp: int):
        self.set_xxx_temp(temp, 11)

    def set_z2_water_temp(self, temp: int):
        self.set_xxx_temp(temp, 15)

    def set_solar_temp(self, temp: int):
        self.set_xxx_temp(temp, 13)
