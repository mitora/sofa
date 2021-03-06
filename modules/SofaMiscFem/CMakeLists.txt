cmake_minimum_required(VERSION 3.1)
project(SofaMiscFem)

set(HEADER_FILES
    BaseMaterial.h
    BoyceAndArruda.h
    Costa.h
    FastTetrahedralCorotationalForceField.h
    FastTetrahedralCorotationalForceField.inl
    HyperelasticMaterial.h
    MooneyRivlin.h
    NeoHookean.h
    Ogden.h
    STVenantKirchhoff.h
    StandardTetrahedralFEMForceField.h
    StandardTetrahedralFEMForceField.inl
    TetrahedralTensorMassForceField.h
    TetrahedralTensorMassForceField.inl
    VerondaWestman.h
    config.h
    initMiscFEM.h
    TetrahedronHyperelasticityFEMForceField.h
    TetrahedronHyperelasticityFEMForceField.inl
    TriangleFEMForceField.h
    TriangleFEMForceField.inl
    TriangularAnisotropicFEMForceField.h
    TriangularAnisotropicFEMForceField.inl
    TriangularFEMForceField.h
    TriangularFEMForceField.inl
)

set(SOURCE_FILES
    FastTetrahedralCorotationalForceField.cpp
    StandardTetrahedralFEMForceField.cpp
    TetrahedralTensorMassForceField.cpp
    initMiscFEM.cpp
    TetrahedronHyperelasticityFEMForceField.cpp
    TriangularAnisotropicFEMForceField.cpp
    TriangleFEMForceField.cpp
    TriangularFEMForceField.cpp
)

if(SOFA_HAVE_NEWMAT)
    list(APPEND HEADER_FILES PlasticMaterial.h)
    list(APPEND SOURCE_FILES PlasticMaterial.cpp)
endif()

add_library(${PROJECT_NAME} SHARED ${HEADER_FILES} ${SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PUBLIC SofaSimpleFem)

if(NOT SOFA_NO_OPENGL)
target_link_libraries(${PROJECT_NAME} PUBLIC SofaOpenglVisual)
endif()

if(SOFA_HAVE_NEWMAT)
    target_link_libraries(${PROJECT_NAME} PUBLIC newmat)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX "_d")
set_target_properties(${PROJECT_NAME} PROPERTIES COMPILE_FLAGS "-DSOFA_BUILD_MISC_FEM")
set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${HEADER_FILES}")

sofa_install_targets(SofaMisc ${PROJECT_NAME} ${PROJECT_NAME})
