# Discrete Wallet

## Build

Clone the wallet and its Discrete core submodule:

```
git clone --recurse-submodules https://github.com/discretecoin/discrete-wallet.git
cd discrete-wallet
```

If the repository was cloned without submodules, initialize the core separately:

```
git submodule update --init --recursive
```

Configure and build:

```
cmake -S . -B build
cmake --build build --config Release --parallel
```
