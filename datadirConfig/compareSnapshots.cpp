#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>

using namespace std;

const int LINE_TO_FIND = 5;

void parseOutpoint(vector<string>& result, const char* filename) {
    string line;
    ifstream f(filename);
    for (int i=0; i<LINE_TO_FIND;i++)
    {
        getline(f,line);
    }
    cout<<"The fifth line is:\n" << line.substr(10, line.size() - 13) << endl;

    stringstream s_stream(line.substr(10, line.size() - 13));
    while(s_stream.good()){
	string op;
	getline(s_stream, op, ')');
	if(op.at(0) == ','){
	    result.push_back(op.substr(2));
	} else {
	    result.push_back(op);
	}
    }
    cout<<"snapshot size is :" << result.size() << endl;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " first_file_name second_file_name" << endl;
        return EXIT_FAILURE;
    }
    vector<string> outpoints1, outpoints2;
    parseOutpoint(outpoints1, argv[1]);
    parseOutpoint(outpoints2, argv[2]);
    cout << "outpoints1 = ";
    for(string s: outpoints1){
	    cout << s << ";";
    }
    cout << endl;
    cout << "outpoints2 = ";
    for(string s: outpoints2){
	    cout << s << ";";
    }
    cout << endl;
    vector<string> intersection_res(outpoints1.size() + outpoints2.size()), difference_res(outpoints1.size());
    auto it=set_intersection(outpoints1.begin(), outpoints1.begin() + outpoints1.size(), outpoints2.begin(), outpoints2.begin() + outpoints2.size(), intersection_res.begin());
    intersection_res.resize(it - intersection_res.begin());
    cout << "unspent set size = " << intersection_res.size() << std::endl;
    it=set_difference(outpoints1.begin(), outpoints1.begin() + outpoints1.size(), outpoints2.begin(), outpoints2.begin() + outpoints2.size(), difference_res.begin());
    difference_res.resize(it - difference_res.begin());
    cout << "spent set size = " << difference_res.size() << std::endl;
    return 0;
}
