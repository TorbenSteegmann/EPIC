cmake_minimum_required(VERSION 3.5)

project(EPIC LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(TBB REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)

add_executable(Fluid_Simulation_Display
    src/main.cc
    src/glad/glad.c
    src/stb_image/stb_image.h
    src/stb_image/stb_image.cc

    # shaders
    res/shaders/sprite.fs res/shaders/sprite.vs
    res/shaders/particle.fs res/shaders/particle.vs
    res/shaders/particle_mesh.fs res/shaders/particle_mesh.vs
    res/shaders/box.fs res/shaders/box.vs
    res/shaders/sky.fs res/shaders/sky.vs
    res/shaders/floor.fs res/shaders/floor.vs

    # ImGui
    include/imgui/imgui.cpp
    include/imgui/imgui_draw.cpp
    include/imgui/imgui_widgets.cpp
    include/imgui/imgui_tables.cpp
    include/imgui/imgui_impl_opengl3.cpp
    include/imgui/imgui_impl_glfw.cpp

    # engine
    src/game.hh src/game.cc
    src/window.hh src/window.cc
    src/shader_program.hh src/shader_program.cc
    src/resource_handler.hh src/resource_handler.cc
    src/texture_2d.hh src/texture_2d.cc
    src/renderer.hh src/renderer.cc
    src/particle_mesher.hh src/particle_mesher.cc
    src/player_controller.hh
    src/game_state.hh
    src/dev_ui.hh src/dev_ui.cc
    src/diagnostics.hh src/diagnostics.cc
    src/camera.hh
    src/defines.hh
    src/world.hh src/world.cc

    # ECS
    src/ecs/ecs_common.hh
    src/ecs/transform_component.hh
    src/ecs/sprite_component.hh
    src/ecs/physics_component.hh
    src/ecs/component_array.hh
    src/ecs/packed_array.hh
    src/ecs/particle_component.hh
    src/ecs/box_component.hh
    src/ecs/collision_component.hh

    # simulation core
    src/simulation/core/grid.hh
    src/simulation/core/helpers.hh src/simulation/core/helpers.cc
    src/simulation/core/mac_grid.hh src/simulation/core/mac_grid.cc
    src/simulation/core/mac_grid_3d.hh src/simulation/core/mac_grid_3d.cc
    src/simulation/core/simulation.hh
    src/simulation/core/timestep_controller.hh src/simulation/core/timestep_controller.cc

    # fluid kernels
    src/simulation/fluid_kernels/add_water_box.hh src/simulation/fluid_kernels/add_water_box.cc
    src/simulation/fluid_kernels/add_taylor_green_vortex.hh src/simulation/fluid_kernels/add_taylor_green_vortex.cc
    src/simulation/fluid_kernels/add_confined_vortex.hh src/simulation/fluid_kernels/add_confined_vortex.cc
    src/simulation/fluid_kernels/angular_momentum.hh src/simulation/fluid_kernels/angular_momentum.cc
    src/simulation/fluid_kernels/advect_3d.hh src/simulation/fluid_kernels/advect_3d.cc
    src/simulation/fluid_kernels/advect.hh src/simulation/fluid_kernels/advect.cc
    src/simulation/fluid_kernels/apply_forces.hh src/simulation/fluid_kernels/apply_forces.cc
    src/simulation/fluid_kernels/enforce_boundary_3d.hh src/simulation/fluid_kernels/enforce_boundary_3d.cc
    src/simulation/fluid_kernels/enforce_boundary.hh src/simulation/fluid_kernels/enforce_boundary.cc
    src/simulation/fluid_kernels/extrapolate_velocity_3d.hh src/simulation/fluid_kernels/extrapolate_velocity_3d.cc
    src/simulation/fluid_kernels/extrapolate_velocity.hh src/simulation/fluid_kernels/extrapolate_velocity.cc
    src/simulation/fluid_kernels/grid_to_particle.hh src/simulation/fluid_kernels/grid_to_particle.cc
    src/simulation/fluid_kernels/grid_to_particle_3d.hh src/simulation/fluid_kernels/grid_to_particle_3d.cc
    src/simulation/fluid_kernels/helpers_3d.hh src/simulation/fluid_kernels/helpers_3d.cc
    src/simulation/fluid_kernels/particle_to_grid.hh src/simulation/fluid_kernels/particle_to_grid.cc
    src/simulation/fluid_kernels/particle_to_grid_3d.hh src/simulation/fluid_kernels/particle_to_grid_3d.cc
    src/simulation/fluid_kernels/polypic.hh src/simulation/fluid_kernels/polypic.cc
    src/simulation/fluid_kernels/projection_3d.hh src/simulation/fluid_kernels/projection_3d.cc
    src/simulation/fluid_kernels/projection.hh src/simulation/fluid_kernels/projection.cc

    # solvers
    src/simulation/fluid_solver.hh src/simulation/fluid_solver.cc
    src/simulation/fluid_solver_3d.hh src/simulation/fluid_solver_3d.cc
    src/simulation/mpm.hh src/simulation/mpm.cc
    src/simulation/mpm_kernels/collocated_grid.hh src/simulation/mpm_kernels/collocated_grid.cc
)

add_executable(Fluid_Particle_Renderer
    src/headless/particle_frame_renderer.cc
    src/glad/glad.c
    src/stb_image/stb_image.h
    src/stb_image/stb_image.cc
    src/stb_image/stb_image_write.h
    src/stb_image/stb_image_write.cc

    res/shaders/sprite.fs res/shaders/sprite.vs
    res/shaders/particle.fs res/shaders/particle.vs
    res/shaders/particle_mesh.fs res/shaders/particle_mesh.vs
    res/shaders/box.fs res/shaders/box.vs
    res/shaders/sky.fs res/shaders/sky.vs
    res/shaders/floor.fs res/shaders/floor.vs

    src/shader_program.hh src/shader_program.cc
    src/resource_handler.hh src/resource_handler.cc
    src/texture_2d.hh src/texture_2d.cc
    src/renderer.hh src/renderer.cc
    src/particle_mesher.hh src/particle_mesher.cc
    src/camera.hh
    src/world.hh src/world.cc

    src/ecs/ecs_common.hh
    src/ecs/transform_component.hh
    src/ecs/sprite_component.hh
    src/ecs/physics_component.hh
    src/ecs/component_array.hh
    src/ecs/packed_array.hh
    src/ecs/particle_component.hh
    src/ecs/box_component.hh
    src/ecs/collision_component.hh
)

add_executable(Fluid_Simulation_Profile
    src/headless/fluid_profile.cc
    src/glad/glad.c
    src/stb_image/stb_image.h
    src/stb_image/stb_image.cc

    # minimal engine/resource support needed by MAC_Grid construction
    src/shader_program.hh src/shader_program.cc
    src/resource_handler.hh src/resource_handler.cc
    src/texture_2d.hh src/texture_2d.cc
    src/world.hh src/world.cc
    src/diagnostics.hh src/diagnostics.cc
    src/particle_frame_export.hh src/particle_frame_export.cc
    src/profile_timer.hh

    # ECS
    src/ecs/ecs_common.hh
    src/ecs/transform_component.hh
    src/ecs/sprite_component.hh
    src/ecs/physics_component.hh
    src/ecs/component_array.hh
    src/ecs/packed_array.hh
    src/ecs/particle_component.hh
    src/ecs/box_component.hh
    src/ecs/collision_component.hh

    # simulation core
    src/simulation/core/grid.hh
    src/simulation/core/helpers.hh src/simulation/core/helpers.cc
    src/simulation/core/mac_grid.hh src/simulation/core/mac_grid.cc
    src/simulation/core/mac_grid_3d.hh src/simulation/core/mac_grid_3d.cc
    src/simulation/core/simulation.hh
    src/simulation/core/timestep_controller.hh src/simulation/core/timestep_controller.cc

    # fluid kernels
    src/simulation/fluid_kernels/add_water_box.hh src/simulation/fluid_kernels/add_water_box.cc
    src/simulation/fluid_kernels/add_taylor_green_vortex.hh src/simulation/fluid_kernels/add_taylor_green_vortex.cc
    src/simulation/fluid_kernels/add_confined_vortex.hh src/simulation/fluid_kernels/add_confined_vortex.cc
    src/simulation/fluid_kernels/angular_momentum.hh src/simulation/fluid_kernels/angular_momentum.cc
    src/simulation/fluid_kernels/advect_3d.hh src/simulation/fluid_kernels/advect_3d.cc
    src/simulation/fluid_kernels/advect.hh src/simulation/fluid_kernels/advect.cc
    src/simulation/fluid_kernels/apply_forces.hh src/simulation/fluid_kernels/apply_forces.cc
    src/simulation/fluid_kernels/enforce_boundary_3d.hh src/simulation/fluid_kernels/enforce_boundary_3d.cc
    src/simulation/fluid_kernels/enforce_boundary.hh src/simulation/fluid_kernels/enforce_boundary.cc
    src/simulation/fluid_kernels/extrapolate_velocity_3d.hh src/simulation/fluid_kernels/extrapolate_velocity_3d.cc
    src/simulation/fluid_kernels/extrapolate_velocity.hh src/simulation/fluid_kernels/extrapolate_velocity.cc
    src/simulation/fluid_kernels/grid_to_particle.hh src/simulation/fluid_kernels/grid_to_particle.cc
    src/simulation/fluid_kernels/grid_to_particle_3d.hh src/simulation/fluid_kernels/grid_to_particle_3d.cc
    src/simulation/fluid_kernels/helpers_3d.hh src/simulation/fluid_kernels/helpers_3d.cc
    src/simulation/fluid_kernels/particle_to_grid.hh src/simulation/fluid_kernels/particle_to_grid.cc
    src/simulation/fluid_kernels/particle_to_grid_3d.hh src/simulation/fluid_kernels/particle_to_grid_3d.cc
    src/simulation/fluid_kernels/polypic.hh src/simulation/fluid_kernels/polypic.cc
    src/simulation/fluid_kernels/projection_3d.hh src/simulation/fluid_kernels/projection_3d.cc
    src/simulation/fluid_kernels/projection.hh src/simulation/fluid_kernels/projection.cc

    # solvers
    src/simulation/fluid_solver.hh src/simulation/fluid_solver.cc
    src/simulation/fluid_solver_3d.hh src/simulation/fluid_solver_3d.cc
)

add_executable(MPM_PolyPIC_Experiment
    src/headless/mpm_polypic_experiment.cc
    src/glad/glad.c
    src/stb_image/stb_image.h
    src/stb_image/stb_image.cc
    src/shader_program.hh src/shader_program.cc
    src/resource_handler.hh src/resource_handler.cc
    src/texture_2d.hh src/texture_2d.cc
    src/world.hh src/world.cc
    src/particle_frame_export.hh src/particle_frame_export.cc
    src/ecs/ecs_common.hh
    src/ecs/transform_component.hh
    src/ecs/sprite_component.hh
    src/ecs/physics_component.hh
    src/ecs/component_array.hh
    src/ecs/packed_array.hh
    src/ecs/particle_component.hh
    src/ecs/box_component.hh
    src/ecs/collision_component.hh
    src/simulation/core/grid.hh
    src/simulation/core/helpers.hh src/simulation/core/helpers.cc
    src/simulation/core/simulation.hh
    src/simulation/fluid_kernels/polypic.hh src/simulation/fluid_kernels/polypic.cc
    src/simulation/mpm.hh src/simulation/mpm.cc
    src/simulation/mpm_kernels/collocated_grid.hh src/simulation/mpm_kernels/collocated_grid.cc
)
target_include_directories(MPM_PolyPIC_Experiment PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(MPM_PolyPIC_Experiment PRIVATE TBB::tbb Eigen3::Eigen)

if(WIN32)
    target_link_libraries(Fluid_Simulation_Display PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/glfw3.lib" TBB::tbb Eigen3::Eigen)
    target_link_libraries(Fluid_Particle_Renderer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/glfw3.lib")

elseif(APPLE)
    target_link_libraries(Fluid_Simulation_Display PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/glfw3.dylib" TBB::tbb)
    target_link_libraries(Fluid_Simulation_Display PRIVATE Eigen3::Eigen)
    target_link_libraries(Fluid_Particle_Renderer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/libs/glfw3.dylib")

else()
    target_link_libraries(Fluid_Simulation_Display PRIVATE glfw TBB::tbb)
    target_link_libraries(Fluid_Particle_Renderer PRIVATE glfw)
endif()

target_include_directories(Fluid_Simulation_Display PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(Fluid_Particle_Renderer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(Fluid_Particle_Renderer PRIVATE FLUID_REPLAY_ONLY=1 _CRT_SECURE_NO_WARNINGS=1)
target_include_directories(Fluid_Simulation_Profile PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(Fluid_Simulation_Profile PRIVATE TBB::tbb Eigen3::Eigen)
target_compile_definitions(Fluid_Simulation_Profile PRIVATE FLUID_PROFILE=1)

if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    foreach(runtime_target Fluid_Simulation_Display Fluid_Simulation_Profile MPM_PolyPIC_Experiment)
        add_custom_command(TARGET ${runtime_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${runtime_target}>
                $<TARGET_FILE_DIR:${runtime_target}>
            COMMAND_EXPAND_LISTS
        )
    endforeach()
endif()

include(GNUInstallDirs)
install(TARGETS Fluid_Simulation_Display
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
