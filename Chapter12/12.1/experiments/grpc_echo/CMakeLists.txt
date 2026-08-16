cmake_minimum_required(VERSION 3.16)
project(csapp_grpc_echo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/echo.proto)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(PROTO_SRCS ${GENERATED_DIR}/echo.pb.cc)
set(PROTO_HDRS ${GENERATED_DIR}/echo.pb.h)
set(GRPC_SRCS ${GENERATED_DIR}/echo.grpc.pb.cc)
set(GRPC_HDRS ${GENERATED_DIR}/echo.grpc.pb.h)

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND protobuf::protoc
  ARGS --grpc_out=${GENERATED_DIR}
       --cpp_out=${GENERATED_DIR}
       -I ${CMAKE_CURRENT_SOURCE_DIR}
       --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
       ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
)

add_library(echo_proto ${PROTO_SRCS} ${GRPC_SRCS})
target_include_directories(echo_proto PUBLIC ${GENERATED_DIR})
target_link_libraries(echo_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)

configure_file(server.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/server.cpp COPYONLY)
configure_file(client.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/client.cpp COPYONLY)

add_executable(grpc_echo_server ${CMAKE_CURRENT_BINARY_DIR}/server.cpp)
target_link_libraries(grpc_echo_server PRIVATE echo_proto)

add_executable(grpc_echo_client ${CMAKE_CURRENT_BINARY_DIR}/client.cpp)
target_link_libraries(grpc_echo_client PRIVATE echo_proto)
