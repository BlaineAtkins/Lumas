#update the coefficient list, then test a brightness and color, and compare the output to the expected output from the table

import numpy as np

def calculate_z(b, c):
    """
    Calculates the fitted z-value for given b (brightness) and c (color)
    using a 10th-order polynomial with 66 coefficients.
    
    Args:
        b (float): The brightness value.
        c (float): The color value.
    
    Returns:
        float: The calculated z-value.
    """

    # The 66 optimized coefficients from your program's output
    params = [
        1.40661474e+03, -8.89724555e-01, -2.10193974e-02, 2.46326838e-04,
        -6.42576675e-07, -2.83601051e-09, 2.34030601e-11, -6.76396117e-14,
        1.00579625e-16, -7.66603184e-20, 2.37064871e-23, -3.89479094e+00,
        -1.19406348e-01, 2.60265737e-03, -1.10591439e-05, -2.41880700e-08,
        2.72285718e-10, -7.31926760e-13, 9.06326708e-16, -5.28297420e-19,
        1.13347866e-22, 9.12202162e-01, 1.59873048e-03, -4.27394787e-05,
        2.72696775e-07, -8.27112834e-10, 1.19205151e-12, -5.85548703e-16,
        -2.64756572e-19, 2.58909964e-22, -1.87959983e-02, 8.22251686e-06,
        2.24891931e-07, -1.03892880e-09, 2.58902993e-12, -4.01288622e-15,
        3.32190622e-18, -1.09541462e-21, 1.51212575e-04, -3.00454234e-07,
        -7.48563771e-10, 2.82924917e-12, -2.63329387e-15, 1.07750517e-18,
        -2.40538516e-22, 3.75189283e-08, 2.75900790e-09, 2.96533441e-14,
        -7.72570754e-15, 5.94040407e-18, -1.08724127e-21, -1.02094005e-08,
        -1.18971541e-11, 1.07818546e-14, 7.04151744e-18, -4.71521571e-21,
        8.40415379e-11, 2.37404048e-14, -3.02032433e-17, 1.28943661e-21,
        -3.27772689e-13, -1.49909539e-17, 2.44321530e-20, 6.49770875e-16,
        -6.80858148e-21, -5.26938205e-19
    ]
    
    k = 0
    total_degree = 10
    
    y_fitted = 0.0
    
    for i in range(total_degree + 1):
        for j in range(total_degree - i + 1):
            y_fitted += params[k] * (b**i) * (c**j)
            k += 1
            
    return y_fitted

# Example usage:
brightness = 26
color = 68
predicted_z = calculate_z(brightness, color)
print(f"Predicted z for b={brightness}, c={color}: {predicted_z}")
