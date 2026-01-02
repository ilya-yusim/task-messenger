#ifndef TASK_HPP
#define TASK_HPP

class MathTask {  // Rename from Task to MathTask
public:
    MathTask(double in1, double in2, char op) : input1(in1), input2(in2), operation(op), result(0) {}

    double getResult() const { return result; }
    void setResult(double res) { result = res; }

    double input1;
    double input2;
    char operation; // '+' for addition, '-' for subtraction
    double result;
};

#endif // TASK_HPP