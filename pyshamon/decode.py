import descriptions


def bit_1(value):
    return value >> 7


def bits_1_and_2(value):
    return (value >> 6) - 1


def bits_3_and_4(value):
    return ((value >> 4) & 0b11) - 1


def bits_5_and_6(value):
    return ((value >> 2) & 0b11) - 1


def bits_7_and_8(value):
    return (value & 0b11) - 1


def bits_3_to_5(value):
    return ((value >> 3) & 0b111) - 1


def left_5_bits(value):
    return (value >> 3) - 1


def right_3_bits(value):
    return (value & 0b111) - 1


def int_minus_1(value):
    return int(value) - 1


def int_minus_128(value):
    return int(value) - 128


def int_minus_1_div_5(value):
    value = ((float(value) - 1) / 5)
    return round(value, 1)


def int_minus_1_times_10(value):
    value = int(value) - 1
    return value * 10


def int_minus_1_times_50(value):
    value = int(value) - 1
    return value * 50


def get_op_mode(value):
    op_mode = int(value & 0b111111)
    if op_mode == 18:
        return "0"
    elif op_mode == 19:
        return "1"
    elif op_mode == 25:
        return "2"
    elif op_mode == 33:
        return "3"
    elif op_mode == 34:
        return "4"
    elif op_mode == 35:
        return "5"
    elif op_mode == 41:
        return "6"
    elif op_mode == 26:
        return "7"
    elif op_mode == 42:
        return "8"
    else:
        return "-1"


def get_energy(value):
    return (int(value) - 1) * 200


def word(b1, b2):
    return b1 * 256 + b2


def get_model(data):
    model = [data[129], data[130], data[131], data[132], data[133], data[134], data[135], data[136], data[137],
             data[138]]
    for i in range(len(descriptions.knownModels)):
        if model == descriptions.knownModels[i]:
            return i
    return -1


def get_pump_flow(data):
    pump_flow1 = int(data[170])
    pump_flow2 = ((float(data[169]) - 1) / 256)
    pump_flow = pump_flow1 + pump_flow2
    return round(pump_flow, 2)


def get_error_info(data):
    error_type = int(data[113])
    error_number = int(data[114]) - 17
    if error_type == 177-128:
        return "F{:02X}".format(error_number)
    elif error_type == 161-128:
        return "H{:02X}".format(error_number)
    else:
        return "?{:02X}:{:02X}".format(error_type, error_number)


def get_inlet_temp(data):
    value = float(int_minus_128(data[143]))
    fractional = int(data[118] & 0b111)
    if fractional == 2:
        value += .25
    elif fractional == 3:
        value += .5
    elif fractional == 4:
        value += .75
    return value


def get_outlet_temp(data):
    value = float(int_minus_128(data[144]))
    fractional = int((data[118] >> 3) & 0b111)
    if fractional == 2:
        value += .25
    elif fractional == 3:
        value += .5
    elif fractional == 4:
        value += .75
    return value


def get_first_byte(value_byte):
    return (value_byte >> 4) - 1


def get_second_byte(value_byte):
    return (value_byte & 0b1111) - 1
