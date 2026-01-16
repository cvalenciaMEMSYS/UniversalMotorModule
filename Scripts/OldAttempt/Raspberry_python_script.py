import serial

import time

import threading

import datetime

import matplotlib.pyplot as plt

import matplotlib.colors as mcolors

import numpy as np

import math

import warnings

import os



# Color manipulation functions

def lighten_color(color, factor=0.5):

    """Lighten a color by blending it with white"""

    rgb = mcolors.to_rgb(color)

    new_rgb = tuple(1 - (1 - c) * (1 - factor) for c in rgb)

    return new_rgb



def darken_color(color, factor=0.5):

    """Darken a color by blending it with black"""

    rgb = mcolors.to_rgb(color)

    new_rgb = tuple(c * (1 - factor) for c in rgb)

    return new_rgb



def get_measurement(filename):

    forces = []

    deflections = []

    loops = []

    

    # Ensure the filename has the correct extension

    if not filename.endswith(".txt"):

        filename += ".txt"

    

    os.chdir("Measurements")

    f = open(filename, "a")

    os.chdir("..")

    

    while True:

        response = ser.readline().decode('utf-8').strip()

        print(response)

        

        if response in ["Measurement completed", "Max force reached, aborting measurement"]:

            forces_array = np.array(forces)

            deflections_array = np.array(deflections)

            loops_array = np.array(loops)

            

            plt.figure(figsize=(7, 5))

            

            unique_loops = np.unique(loops_array)

            legend_handles = []

            legend_labels = []



            for i, loop in enumerate(unique_loops):

                mask = loops_array == loop

                loop_forces = forces_array[mask]

                loop_deflections = deflections_array[mask]



                split_idx = math.ceil(len(loop_forces) / 2)

                ext_def = loop_deflections[:split_idx]

                ext_force = loop_forces[:split_idx]

                ret_def = loop_deflections[split_idx:]

                ret_force = loop_forces[split_idx:]



                base_color = plt.cm.tab10(i % 10)

                ext_color = darken_color(base_color, 0.3)

                ret_color = lighten_color(base_color, 0.3)



                # Plot the extension and retraction lines

                ext_line, = plt.plot(ext_def, ext_force, color=ext_color, linewidth=2, label=f"Loop {int(loop)} Ext.")

                ret_line, = plt.plot(ret_def, ret_force, color=ret_color, linewidth=2, label=f"Loop {int(loop)} Ret.")



                legend_handles.append(ext_line)

                legend_labels.append(f"Loop {int(loop)} Ext.")

                legend_handles.append(ret_line)

                legend_labels.append(f"Loop {int(loop)} Ret.")



            plt.ylabel("Force [N]", fontsize=12)

            plt.xlabel("Deflection [mm]", fontsize=12)

            plt.grid(True, alpha=0.3)



            plt.legend(legend_handles, legend_labels, loc='upper right', frameon=False)

            

            # Save data

            f.write("Force <N>\tDeflection <mm>\tLoop\n")

            for i in range(len(forces)):

                f.write(f"{forces[i]}\t{deflections[i]}\t{loops[i]}\n")

            

            f.close()

            

            plt.tight_layout()

            plt.show()

            break

        

        try:

            values = response.split("|")

            if len(values) == 3:

                force = float(values[0]) * -1

                deflection = float(values[1])

                loop = int(values[2])

                

                forces.append(force)

                deflections.append(deflection)

                loops.append(loop)

        except Exception as e:

            print(f"Error processing data: {e}")



def read_response():

    filename = None  # Initialize filename storage

    

    while True:

        response = ser.readline().decode('utf-8').strip()

        print(response)

        

        if response.startswith("FILENAME|"):  # Detect filename marker

            filename = response.split("|")[1]

            print(f"Received filename: {filename}")



        if response == "----------" and filename:  # Start measurement with filename

            get_measurement(filename)



def write_input():

    while True:

        command = input()

        ser.write(command.encode('utf-8'))



########################################################################



warnings.simplefilter("ignore", UserWarning)



ser = serial.Serial('/dev/ttyACM0', 115200)

time.sleep(2)



if not os.path.isdir("Measurements"):

    os.makedirs("Measurements")



if __name__ == "__main__":

    t1 = threading.Thread(target=write_input)

    t2 = threading.Thread(target=read_response)

 

    t1.start()

    t2.start()

 

    t1.join()

    t2.join()

    

ser.close()