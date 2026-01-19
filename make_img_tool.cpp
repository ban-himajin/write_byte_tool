#include<iostream>
#include<string>
#include<vector>
#include<filesystem>
#include<fstream>
#include<cstdint>
#include<sstream>
#include<array>
#include<cstddef>
#include<cstring>
#include<algorithm>

#define VERSION "1.0.0"
#define IMGNAME "disk.img"

using namespace std;

namespace fs = filesystem;

//位置を指定して書き込んだファイルログ構造体
struct writefilelog{
    string name;
    uint64_t offset = 0;
    uint64_t size = 0;
    bool result = false;
};

//バイト位置を指定した書き込んだバイトログ構造体
struct writebyte{
    uint64_t select_offset = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t num = 0;
    char endian = 'l';
    bool result = false;
};

//使用するモード、書き込むデータ内容を保持する構造体
struct settings{
    /*mode:
        1 = オート(自動安全モード)[実装予定]
        2 = マニュアル(小規模安全モード)[実装予定]
        3 = プロ(安全モード無効)[実装済み]
    */
   uint8_t mode = 1;
   /*type:
       1 = 最低限サイズ確保[実装済み]
       2 = disk_size分確保[実装予定]
   */
    uint8_t type = 1;
    bool MBR = false;
    bool VBR = false;
    bool GPT = false;
    uint64_t target_disk_size = 0;
};

//設定ファイルの文字列解析
bool get_setting_parameter(const string& setting_file, vector<string>& parameter_names, vector<string>& parameter){
    ifstream file(setting_file);
    if(!file)return false;
    string string_line;
    while(getline(file,string_line)){
        stringstream ss(string_line);
        string string_text;
        if(getline(ss,string_text,':')){
            if(string_line.size() != string_text.size()){
                parameter_names.push_back(string_text);
                if(getline(ss,string_text))parameter.push_back(string_text);
            }
        }
    }
    return true;
}

bool writebinaryfile(vector<writefilelog>& filelogs, ofstream& img){
    cout << filelogs.size() << endl;
    if(filelogs.size() != 1){
        //ファイル書き込みによる書き込んだファイルの上書きを阻止
        //ここから先 filelogs すべての要素の offset,size を確認したうえで安全かを確認するシステムの導入予定
        //現在使用:書き込むファイル要素は昇順に書き込む必要がある
        if(filelogs.at(filelogs.size()-2).size > filelogs.at(filelogs.size()-1).offset)return false;
    }
    ifstream bin_file(filelogs.at(filelogs.size()-1).name, ios::binary);
    try{
        uint64_t size = fs::file_size(filelogs.at(filelogs.size()-1).name);
        filelogs.at(filelogs.size()-1).size = size;
        vector<unsigned char> buffer(size);
        bin_file.read(reinterpret_cast<char*>(buffer.data()), size);
        img.seekp(0, ios::end);
        auto img_size = static_cast<int64_t>(img.tellp());
        if(img_size < 0){
            cerr << "img file break" << endl;
            return false;
        }
        if(filelogs.at(filelogs.size()-1).offset > img_size){
            char zero = 0;
            for(uint64_t i = 0; i < filelogs.at(filelogs.size()-1).offset-img_size;i++)img.write(&zero,1);
        }
        img.seekp(filelogs.at(filelogs.size()-1).offset, ios::beg);
        img.write(reinterpret_cast<char*>(buffer.data()), size);
        return true;
    }catch(const fs::filesystem_error& e){
        cerr << "filesystem error:" << e.what() << endl;
        return false;
    }
}

bool writebinary(const writebyte& bytelogs,const vector<writefilelog>& filelogs, ofstream& img){
    try{
        if(filelogs.at(bytelogs.select_offset).result == false){
            cerr << "select write file offset error" << endl;
            cerr << "error structs" << endl;
            cerr << "name:" << filelogs.at(bytelogs.select_offset).name << endl;
            cerr << "offset:" << filelogs.at(bytelogs.select_offset).offset << endl;
            cerr << "size:" << filelogs.at(bytelogs.select_offset).size << endl;
            cerr << "result:" << filelogs.at(bytelogs.select_offset).result << endl;
            return false;
        }

        //struct menber check
        if(bytelogs.size == 0){//select size check
            cerr << "no select size" << endl;
            return false;
        }

        uint64_t seek = filelogs.at(bytelogs.select_offset).offset;
        seek += bytelogs.offset;
        auto img_size = static_cast<int64_t>(img.tellp());
        if(img_size < 0){
            cerr << "img file break" << endl;
            return false;
        }
        
        vector<unsigned char>writebyte(bytelogs.size, 0);
        cout << writebyte.size() << endl;
        cout << bytelogs.num << endl;
        cout << bytelogs.size*8 << endl;
        cout << (1ULL<<(bytelogs.size*8)) << endl;
        if(bytelogs.size >= 8)cout << "byte check off" << endl;
        else if(bytelogs.num >= (1ULL<<(bytelogs.size*8)))return false;
        memcpy(reinterpret_cast<char*>(writebyte.data()), &bytelogs.num, bytelogs.size);
        if(bytelogs.endian == 'b')reverse(writebyte.begin(), writebyte.end());
        for(const auto f : writebyte)cout << hex << static_cast<int>(f) << " ";
        cout << "\nend" << endl;
        if(img_size >= seek){
            img.seekp(seek, ios::beg);
            img.write(reinterpret_cast<char*>(writebyte.data()), bytelogs.size);
            return true;
        }
        return true;
    }catch(const fs::filesystem_error& e){
        cerr << "filesystem error:" << e.what() << endl;
        return false;
    }catch(const out_of_range& e){
        cerr << "out of range:" << e.what() << endl;
        return false;
    }
}

bool partition_set(const vector<string>& parameter_names, const vector<string>& parameter){
    string disk_name = IMGNAME;
    vector<writefilelog> filelogs;
    vector<writebyte> bytelogs;
    ofstream img(disk_name, ios::binary);
    if(!img)return false;
    uint64_t set_bainary = 0;
    for(uint64_t i = 0;i<parameter.size();i++){
        cout << parameter_names.at(i) << endl;
        try{
            if(parameter_names.at(i) == "set_binary")set_bainary = stoull(parameter.at(i));
            if(parameter_names.at(i).find("_file") != string::npos){
                filelogs.push_back({parameter.at(i), set_bainary});
                filelogs.at(filelogs.size()-1).result = writebinaryfile(filelogs, img);
            }
            try{
                if(parameter_names.at(i) == "select_offset")bytelogs.push_back({stoull(parameter.at(i))});
            }catch(const invalid_argument& e){
                cerr << "minus num access:" << e.what() << endl;
                return false;
            }
            if(parameter_names.at(i) == "offset")bytelogs.at(bytelogs.size()-1).offset = stoll(parameter.at(i));
            if(parameter_names.at(i) == "size")bytelogs.at(bytelogs.size()-1).size = stoll(parameter.at(i));
            if(parameter_names.at(i) == "endian")bytelogs.at(bytelogs.size()-1).endian = parameter.at(i)[0];
            if(parameter_names.at(i) == "num"){
                bytelogs.at(bytelogs.size()-1).num = stoull(parameter.at(i));
                bytelogs.at(bytelogs.size()-1).result = writebinary(bytelogs.at(bytelogs.size()-1), filelogs, img);
            }
        }catch(const invalid_argument& e){
            cerr << "cannot convert to number:" << e.what() << endl;
            return false;
        }catch(const out_of_range& e){
            cerr << "Number out of range:" << e.what() << endl;
            return false;
        }
    }

    return true;
}

int main(int argc, char* argv[]){
    vector<string> parameter_names;
    vector<string> parameter;
    fs::path main_file = argv[0];
    string setting_file = main_file.stem().string() + "_setting.txt";
    if(!get_setting_parameter(setting_file,parameter_names,parameter)){
        cout << "not open " << setting_file << endl;
        return 1;
    }
    cout << "found " << setting_file << endl;
    if(!partition_set(parameter_names,parameter)){
        cout << "error " << IMGNAME << endl;
        return 1;
    }
    return 0;
}