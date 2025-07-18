# 3D Fluid Simulation

https://github.com/user-attachments/assets/1ad166a8-c712-4bff-95cf-cbb520181d00

3D Fluid Simulation using SDL3 GPU with compute shaders

### Building

#### Windows

Install the [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) for glslc

```bash
git clone https://github.com/jsoulier/3d_fluid_simulation --recurse-submodules
cd 3d_fluid_simulation
mkdir build
cd build
cmake ..
cmake --build . --parallel 8 --config Release
cd bin
./fluid_simulation.exe
```

#### Linux

```bash
git clone https://github.com/jsoulier/3d_fluid_simulation --recurse-submodules
cd 3d_fluid_simulation
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel 8
cd bin
./fluid_simulation
```

### Gallery

#### Images From Video

![](doc/image1.png)
*Velocity (X)*
![](doc/image2.png)
*Velocity (Y)*
![](doc/image3.png)
*Velocity (Z)*
![](doc/image4.png)
*Density*
![](doc/image5.png)
*Combined*

#### Offset Opposing

![](doc/image6.png)
*Velocity (Y)*
![](doc/image7.png)
*Combined*

#### Unbalanced Opposing

![](doc/image8.png)
*Velocity (Y)*
![](doc/image9.png)
*Combined*

#### Intersecting

![](doc/image10.png)
*Velocity (X)*
![](doc/image11.png)
*Velocity (Y)*
![](doc/image12.png)
*Velocity (Z)*
![](doc/image13.png)
*Combined*

#### Other

https://github.com/user-attachments/assets/da450974-2e93-45e5-acac-0d70a1bbebc6

### References

- [Fluid Simulation for Dummies](https://mikeash.com/pyblog/fluid-simulation-for-dummies.html) by Mike Ash
