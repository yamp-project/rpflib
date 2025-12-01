### Example usage
Small example for reading from an RPF file:
```c++
auto archive = rpflib::RPF7Archive::OpenArchive("./t3rm_hurricane.rpf");
std::filesystem::path outputPath = std::filesystem::current_path() / "test_hurricane_output"; //output path where we want to extract entry files

//iterating through the entries as a relative path
for (auto& path : archive->GetEntryList())
{
    //remove the first slash at the beginning
    std::string relativePath = path.substr(1, path.size());
    
    //setup the output path
    std::filesystem::path fullOutputPath = (outputPath / relativePath);
    
    //extract entry file as a relative path to the actual full output path
    printf("Create: %d\n", archive->SaveEntryToPath(path, fullOutputPath));
}

//close the archive when we are done
archive->CloseArchive();
```

Small example for writing an RPF archive:
```c++
auto archiveWrite = rpflib::RPF7Archive::CreateArchive("./t3rm_hurricane_test.rpf");

//looping through the path recursively
//(we will just use the example path where we extracted all the entries in the reading example)
std::filesystem::path outputPath = std::filesystem::current_path() / "test_hurricane_output";
for (auto& entry : std::filesystem::recursive_directory_iterator(outputPath))
{
    //check if given entry is indeed a file
    if (!std::filesystem::is_regular_file(entry))
        continue;

    //make a relative path that we can use for the archive
    std::filesystem::path relativePath = std::filesystem::relative(entry, outputPath);
    
    //correcting backslashes to forward slashes
    relativePath = rpflib::RPF7Archive::CorrectEntryPath(relativePath);
    
    //add a new entry based on it's relative path and file path
    archiveWrite->AddEntry(relativePath, entry);
}

// closing the file is necessary
// as it will build the RPF file
// and write all the entry data into the archive
archiveWrite->CloseArchive();
```