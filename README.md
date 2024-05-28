# Project

LogCheetah is a windows gui tool for viewing column-based log data, optimized for interacting with large volumes (millions of rows) of data in memory at once.

Supported data types:
- Json: Data is flattened into columns named after the parent node names.
- Comma/Space/Tab/Pipe-seperated values.
- Trx: VsTest result xml files.  This type is not performance optimized.

## Building

This repo uses vcpkg as a submodule.  It will need bootstraped and installed prior to building normally.  Complete build steps:
1. ```.\vcpkg\bootstrap-vcpkg.bat```
1. ```.\vcpkg\vcpkg install --triplet x64-windows```
1. ```mkdir build & cd build```
1. ```cmake ..```
1. ```cmake --build . --config=Release```


## Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit https://cla.opensource.microsoft.com.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft 
trademarks or logos is subject to and must follow 
[Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.
