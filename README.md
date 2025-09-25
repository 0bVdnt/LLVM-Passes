## LLVM-Passes

### Installation and running

- llvm-20 for New Pass Manager (NPM)

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 20
```

or

```bash
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
echo "deb http://apt.llvm.org/$(lsb_release -cs)/ llvm-toolchain-$(lsb_release -cs)-20 main" | sudo tee /etc/apt/sources.list.d/llvm.list
sudo apt update
sudo apt install llvm-20-dev clang-20 lld-20
```

- Create a build directory and follow the commands

```bash
mkdir build
cd build
cmake ..
cmake --build .
cd .. # Move to project directory
# Compile using clang-20
# O1 flag for optimization
clang-20 -O1 -S -emit-llvm test_program.c -o test_program.ll
opt-20 -load-pass-plugin ./build/lib/libHelloWorldPass.so -passes=hello-world < test_program.ll > /dev/null
```

### Expected Output:

```bash
Hello World Pass (NPM): Analyzing function -> sayHello
Hello World Pass (NPM): Analyzing function -> add
Hello World Pass (NPM): Analyzing function -> main

```
