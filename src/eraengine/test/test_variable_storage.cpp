#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include "variable_storage.h"
#include "variable_types.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    
    qDebug() << "Variable Storage Test";
    qDebug() << "=====================";
    
    // Test 1: Create a variable storage instance
    qDebug() << "\n1. Creating VariableStorage:";
    VariableStorage storage;
    qDebug() << "   VariableStorage created successfully";
    
    // Test 2: Initialize storage
    qDebug() << "\n2. Initializing storage:";
    storage.initialize(100, 100);
    qDebug() << "   Storage initialized with maxCharacters=100, localSize=100";
    
    // Test 3: Test global integer variables (1D)
    qDebug() << "\n3. Testing Global Integer 1D Variables:";
    QString globalInt1DName = "TEST_GLOBAL_INT_1D";
    storage.setGlobalInt1D(globalInt1DName, 0, 42);
    qDebug() << "   storage.setGlobalInt1D(\"" << globalInt1DName << "\", 0, 42)";
    qint64 value1D = storage.getGlobalInt1D(globalInt1DName, 0);
    qDebug() << "   storage.getGlobalInt1D(\"" << globalInt1DName << "\", 0) = " << value1D;
    
    // Test 4: Test global integer variables (2D)
    qDebug() << "\n4. Testing Global Integer 2D Variables:";
    QString globalInt2DName = "TEST_GLOBAL_INT_2D";
    storage.setGlobalInt2D(globalInt2DName, 0, 0, 100);
    qDebug() << "   storage.setGlobalInt2D(\"" << globalInt2DName << "\", 0, 0, 100)";
    qint64 value2D = storage.getGlobalInt2D(globalInt2DName, 0, 0);
    qDebug() << "   storage.getGlobalInt2D(\"" << globalInt2DName << "\", 0, 0) = " << value2D;
    
    // Test 5: Test global integer variables (3D)
    qDebug() << "\n5. Testing Global Integer 3D Variables:";
    QString globalInt3DName = "TEST_GLOBAL_INT_3D";
    storage.setGlobalInt3D(globalInt3DName, 0, 0, 0, 200);
    qDebug() << "   storage.setGlobalInt3D(\"" << globalInt3DName << "\", 0, 0, 0, 200)";
    qint64 value3D = storage.getGlobalInt3D(globalInt3DName, 0, 0, 0);
    qDebug() << "   storage.getGlobalInt3D(\"" << globalInt3DName << "\", 0, 0, 0) = " << value3D;
    
    // Test 6: Test character integer variables (2D)
    qDebug() << "\n6. Testing Character Integer Variables (2D):";
    QString charaIntName = "TEST_CHARA_INT";
    storage.setCharaInt(charaIntName, 1, 0, 300);
    qDebug() << "   storage.setCharaInt(\"" << charaIntName << "\", 1, 0, 300)";
    qint64 charaValue = storage.getCharaInt(charaIntName, 1, 0);
    qDebug() << "   storage.getCharaInt(\"" << charaIntName << "\", 1, 0) = " << charaValue;
    
    // Test 7: Test character integer variables (3D)
    qDebug() << "\n7. Testing Character Integer Variables (3D):";
    QString charaInt3DName = "TEST_CHARA_INT_3D";
    storage.setCharaInt3D(charaInt3DName, 1, 0, 0, 400);
    qDebug() << "   storage.setCharaInt3D(\"" << charaInt3DName << "\", 1, 0, 0, 400)";
    qint64 charaValue3D = storage.getCharaInt3D(charaInt3DName, 1, 0, 0);
    qDebug() << "   storage.getCharaInt3D(\"" << charaInt3DName << "\", 1, 0, 0) = " << charaValue3D;
    
    // Test 8: Test local integer variables
    qDebug() << "\n8. Testing Local Integer Variables:";
    storage.setLocalInt(0, 500);
    qDebug() << "   storage.setLocalInt(0, 500)";
    qint64 localInt = storage.getLocalInt(0);
    qDebug() << "   storage.getLocalInt(0) = " << localInt;
    
    // Test 9: Test local string variables
    qDebug() << "\n9. Testing Local String Variables:";
    storage.setLocalStr(0, "Hello World");
    qDebug() << "   storage.setLocalStr(0, \"Hello World\")";
    QString localStr = storage.getLocalStr(0);
    qDebug() << "   storage.getLocalStr(0) = \"" << localStr << "\"";
    
    // Test 10: Test system variables
    qDebug() << "\n10. Testing System Variables (DAY):";
    storage.setDay(0, 1000);
    qDebug() << "   storage.setDay(0, 1000)";
    qint64 dayValue = storage.getDay(0);
    qDebug() << "   storage.getDay(0) = " << dayValue;
    
    qDebug() << "\n11. Testing System Variables (MONEY):";
    storage.setMoney(0, 5000);
    qDebug() << "   storage.setMoney(0, 5000)";
    qint64 moneyValue = storage.getMoney(0);
    qDebug() << "   storage.getMoney(0) = " << moneyValue;
    
    qDebug() << "\n12. Testing System Variables (FLAG):";
    storage.setFlag(0, 1);
    qDebug() << "   storage.setFlag(0, 1)";
    qint64 flagValue = storage.getFlag(0);
    qDebug() << "   storage.getFlag(0) = " << flagValue;
    
    // Test 11: Test variable type checking
    qDebug() << "\n13. Testing Variable Type Checking:";
    QString checkName = "TEST_CHECK";
    storage.setGlobalInt1D(checkName, 0, 123);
    qDebug() << "   storage.setGlobalInt1D(\"" << checkName << "\", 0, 123)";
    bool isInt = storage.isVariableInteger(checkName);
    qDebug() << "   storage.isVariableInteger(\"" << checkName << "\") = " << (isInt ? "true" : "false");
    bool isStr = storage.isVariableString(checkName);
    qDebug() << "   storage.isVariableString(\"" << checkName << "\") = " << (isStr ? "true" : "false");
    
    // Test 12: Test dimension checking
    qDebug() << "\n14. Testing Variable Dimension Checking:";
    bool is1D = storage.isVariable1D(checkName);
    qDebug() << "   storage.isVariable1D(\"" << checkName << "\") = " << (is1D ? "true" : "false");
    
    // Test 13: Test expression evaluation
    qDebug() << "\n15. Testing Expression Evaluation:";
    QString expr = "10 + 20";
    QVariant exprResult = storage.evaluateExpression(expr);
    qDebug() << "   storage.evaluateExpression(\"" << expr << "\") = " << exprResult.toString();
    
    expr = "DAY + 100";
    exprResult = storage.evaluateExpression(expr);
    qDebug() << "   storage.evaluateExpression(\"" << expr << "\") = " << exprResult.toString();
    
    // Test 14: Test variable type enumeration
    qDebug() << "\n16. Testing Variable Types Enumeration:";
    qDebug() << "   VariableTypes::Type::Integer =" << static_cast<int>(VariableTypes::Type::Integer);
    qDebug() << "   VariableTypes::Type::String =" << static_cast<int>(VariableTypes::Type::String);
    qDebug() << "   VariableTypes::Type::Array1D =" << static_cast<int>(VariableTypes::Type::Array1D);
    qDebug() << "   VariableTypes::Type::Array2D =" << static_cast<int>(VariableTypes::Type::Array2D);
    qDebug() << "   VariableTypes::Type::Array3D =" << static_cast<int>(VariableTypes::Type::Array3D);
    qDebug() << "   VariableTypes::Type::CharacterData =" << static_cast<int>(VariableTypes::Type::CharacterData);
    qDebug() << "   VariableTypes::Type::Local =" << static_cast<int>(VariableTypes::Type::Local);
    qDebug() << "   VariableTypes::Type::Global =" << static_cast<int>(VariableTypes::Type::Global);
    qDebug() << "   VariableTypes::Type::Unchangeable =" << static_cast<int>(VariableTypes::Type::Unchangeable);
    qDebug() << "   VariableTypes::Type::Calc =" << static_cast<int>(VariableTypes::Type::Calc);
    qDebug() << "   VariableTypes::Type::Constant =" << static_cast<int>(VariableTypes::Type::Constant);
    
    // Test 15: Test scope enumeration
    qDebug() << "\n17. Testing Variable Scopes Enumeration:";
    qDebug() << "   VariableTypes::Scope::Local =" << static_cast<int>(VariableTypes::Scope::Local);
    qDebug() << "   VariableTypes::Scope::Global =" << static_cast<int>(VariableTypes::Scope::Global);
    qDebug() << "   VariableTypes::Scope::CharacterData =" << static_cast<int>(VariableTypes::Scope::CharacterData);
    
    // Test 16: Test dimension enumeration
    qDebug() << "\n18. Testing Variable Dimensions Enumeration:";
    qDebug() << "   VariableTypes::Dimension::None =" << static_cast<int>(VariableTypes::Dimension::None);
    qDebug() << "   VariableTypes::Dimension::OneD =" << static_cast<int>(VariableTypes::Dimension::OneD);
    qDebug() << "   VariableTypes::Dimension::TwoD =" << static_cast<int>(VariableTypes::Dimension::TwoD);
    qDebug() << "   VariableTypes::Dimension::ThreeD =" << static_cast<int>(VariableTypes::Dimension::ThreeD);
    
    qDebug() << "\nVariable storage test complete!";
    
    return 0;
}
