import numpy as np
from scipy.optimize import curve_fit
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import csv


def surface_model(data, p0, p1, p2, p3, p4, p5):
    """
    A quadratic surface model for fitting.
    data: a tuple or list containing the brightness and color arrays.
    p0-p5: the parameters of the model.
    """
    brightness, color = data
    return p0 + p1 * brightness + p2 * color + p3 * brightness**2 + p4 * color**2 + p5 * brightness * color

#this is the surface model with more polynomials for a hopefully better fit
def surface_model_cubic(data, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9):
    """
    A cubic surface model for fitting.
    data: a tuple or list containing the brightness and color arrays.
    p0-p9: the parameters of the model.
    """
    brightness, color = data
    return (
        p0 + p1 * brightness + p2 * color +
        p3 * brightness**2 + p4 * color**2 + p5 * brightness * color +
        p6 * brightness**3 + p7 * color**3 + p8 * brightness**2 * color + p9 * brightness * color**2
    )

def polynomial_surface_model(data, *params):
    """
    A dynamic polynomial surface model for fitting.
    data: a tuple containing the brightness and color arrays.
    params: a variable number of parameters (coefficients).
    """
    brightness, color = data
    k = 0
    total_degree = 10 # Change this to the desired degree
    
    y_fitted = np.zeros_like(brightness)
    
    for i in range(total_degree + 1):
        for j in range(total_degree - i + 1):
            term = (brightness**i) * (color**j)
            y_fitted += params[k] * term
            k += 1
            
    return y_fitted


#read in raw data and flatten it in to 1D arrays. The input file can be created by running the data collection process -- PhotoResistorSendValues on the Lumas and PhotoResistorSendValues on a PC
brightness=[]
color=[]
measured_brightness=[]


currentRow=0
with open('RawDataPhotoResistor.csv', mode='r', newline='') as file:
    csv_reader = csv.reader(file)
    for row in csv_reader:
        currentRow+=1
        currentColor=currentRow-2

        if(currentRow>1):
            for currentColumn in range(1,129):
                currentBrightness=(currentColumn-1)*2
                
                #print(row[currentColumn])
                brightness.append(int(currentBrightness))
                color.append(int(currentColor))
                measured_brightness.append(int(row[currentColumn]))
print("read in raw data with "+str(currentRow)+" rows and flattened in to arrays. Total of "+str(len(brightness)+len(color)+len(measured_brightness))+" values!")
print("Will now generate a surface fit for the data. This will take some time...")
                
#Previously a seperate program flattened the arrays and saved them to a CSV that this program read in. Now that's done within this program so the code below is no longer needed
#read in 1D arrays from file cause they are MASSIVE
##with open('brightnessList.csv', mode='r', newline='') as file:
##    csv_reader = csv.reader(file)
##    for row in csv_reader:
##        brightness=[int(s) for s in row]
##with open('colorList.csv', mode='r', newline='') as file:
##    csv_reader = csv.reader(file)
##    for row in csv_reader:
##        color=[int(s) for s in row]
##with open('measured_brightnessList.csv', mode='r', newline='') as file:
##    csv_reader = csv.reader(file)
##    for row in csv_reader:
##        measured_brightness=[int(s) for s in row]
##print("Read in three 1D arrays, each with length "+str(len(brightness)))


# Create some sample data for demonstration
# Replace this with your actual data
num_points = len(brightness)
np.random.seed(0)
#brightness = [0,2,1,4,8,6,7,8,9,10]
#color = [0,2,3,4,5,6,7,8,9,10]
# The true measured brightness plus some noise
#true_measured_brightness = 2 + 0.5 * brightness - 0.3 * color + 0.1 * brightness**2 + 0.2 * color**2 - 0.05 * brightness * color
#measured_brightness = true_measured_brightness + 0.5 * np.random.randn(num_points)
#measured_brightness = [0,0,0,0,0,0,0,0,0,1]


# Use curve_fit to find the optimal parameters
# The 'popt' variable will contain the optimized parameters
# 'pcov' is the covariance matrix of the parameters

#below three lines was original surface with fewer polynomials
#popt, pcov = curve_fit(surface_model, (brightness, color), measured_brightness)
#print("Optimized parameters (p0 to p5):")
#print(popt)

#below three lines using 3rd order polynomial
#popt, pcov = curve_fit(surface_model_cubic, (brightness, color), measured_brightness)
#print("Optimized parameters (p0 to p9):")
#print(popt)


initial_guess = np.ones(66) # Provide an initial guess for each coefficient
popt, pcov = curve_fit(polynomial_surface_model, (brightness, color), measured_brightness, p0=initial_guess)
print("Optimized parameters:")
print(popt)


#z_min=-10
#z_max=10

# Create a grid for plotting the fitted surface
# This creates a smooth surface for visualization
brightness_grid, color_grid = np.meshgrid(
    np.linspace(min(brightness), max(brightness), 50),
    np.linspace(min(color), max(color), 50)
)
fitted_surface = polynomial_surface_model(
    (brightness_grid, color_grid), *popt
)

#clip surface to only be shown within chart space. This *kind of* works...
##fitted_surface = np.where(
##    (fitted_surface >= z_min-1) & (fitted_surface <= z_max+1),
##    fitted_surface,
##    np.nan
##)

# Plot the original data points and the fitted surface
fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
#ax.set_zlim(z_min, z_max)
# Plot original data points as a scatter plot
ax.scatter(brightness, color, measured_brightness, c='blue', label='Original Data')
# Plot the fitted surface
ax.plot_surface(brightness_grid, color_grid, fitted_surface, alpha=0.5, color='red', label='Fitted Surface')

ax.set_xlabel('Brightness')
ax.set_ylabel('Color')
ax.set_zlabel('Measured Brightness')
ax.set_title('3D Surface Fit')
plt.show()


