%{
IGNORE THIS BLOCK
speed_of_sound = 344.026;
alpha = 0.385725477;
w_actual = speed_of_sound/cos(alpha); %371.3076
%}


mm2in = 0.0393700787;
in2mm = 1/0.0393700787;
m2in = mm2in * 1000;
in2m = in2mm / 1000;

% Microphone Coordinates
d1x = 0;
d2x = 0;
d3x = 1.5628;
d4x = 1.5628;
d1y = 0;
d2y = 0.8084;
d3y = 0.5910;
d4y = 1.3994;

x_max = d3x;
y_max = d4y;

% Given time delay (D1 = T1-T2, etc)
% T1 = 8819491/2*1e-6;
% T2 = 678142/2*1e-6;
% T3 = 9042115/2*1e-6;
% T4 = 42019600/2*1e-6;
D1 = -0.0004362973;
D2 = 0.0025962968;
D3 = -0.0017964437;

%{ 
IGNORE THIS BLOCK
syms R1 R2 R3 R4 x y w
eq1 = R1^2 == (x-d1x)^2+(y-d1y)^2;
eq2 = R2^2 == (x-d2x)^2+(y-d2y)^2;
eq3 = R3^2 == (x-d3x)^2+(y-d3y)^2;
eq4 = R4^2 == (x-d4x)^2+(y-d4y)^2;
eq5 = R1-R2 == D1*w;
eq6 = R2-R3 == D2*w;
eq7 = R3-R4 == D3*w;
sol = solve([eq1, eq2, eq3, eq4, eq5, eq6, eq7], [R1, R2, R3, R4, x, y, w]);
x_vpa = vpa(sol.x);
y_vpa = vpa(sol.y);
w_vpa = vpa(sol.w);
R1_vpa = vpa(sol.R1);
%}

% Constants
delta1 = D1;
delta2 = D1+D2;
delta3 = D1+D2+D3;

% Constants
K1 = d1x^2-d2x^2+d1y^2-d2y^2;
K2 = d1x^2-d3x^2+d1y^2-d3y^2;
K3 = d1x^2-d4x^2+d1y^2-d4y^2;

% Coefficient matrix
A = [-2*d1x+2*d2x, -2*d1y+2*d2y, -2*delta1;
    -2*d1x+2*d3x, -2*d1y+2*d3y, -2*delta2;
    -2*d1x+2*d4x, -2*d1y+2*d4y, -2*delta3];

% Check that A is invertible
if det(A) == 0
    error('Coefficient matrix A is singular. Inverse cannot be computed');
end

% Calculate inverse of matrix A
B=inv(A);

% Constants referencing the inverse (B) of A
C1 = -B(1,1)*K1-B(1,2)*K2-B(1,3)*K3;
C2 = -B(2,1)*K1-B(2,2)*K2-B(2,3)*K3;
C3 = -B(3,1)*K1-B(3,2)*K2-B(3,3)*K3;

% Constants referencing the inverse (B) of A
Q1 = -B(1,1)*delta1^2-B(1,2)*delta2^2-B(1,3)*delta3^2;
Q2 = -B(2,1)*delta1^2-B(2,2)*delta2^2-B(2,3)*delta3^2;
Q3 = -B(3,1)*delta1^2-B(3,2)*delta2^2-B(3,3)*delta3^2;

% Constants
N1 = C1-d1x;
N2 = C2-d2x;

% Constants
coef1 = Q1^2+Q2^2;
coef2 = -Q3^2+2*N1*Q1+2*N2*Q2;
coef3 = -2*C3*Q3+N1^2+N2^2;
coef4 = -C3^2;

% Find the roots of 3rd order polynomial
r = roots([coef1, coef2, coef3, coef4]);

% Ensure all found roots are positive (must always be true)
for i = 1:3
    if r(i) < 0
        r(i) = -1*r(i);
    end
end

% Find all possible values of w
w_array = sqrt(r);

% Solve for all possible values of all variables
x_array = C1+w_array.^2*Q1;
y_array = C2+w_array.^2*Q2;
U_array = C3+w_array.^2*Q3;
R1_array = U_array./w_array;
R2_array = R1_array -D1*w_array;
R3_array = R2_array -D2*w_array;
R4_array = R3_array -D3*w_array;

% Can delete count variable here and in loop
% This loop checks all values to ensure they fit initial equations
count = 0;
for i = 1:length(w_array)
    calc_R1 = sqrt((x_array(i)-d1x)^2 + (y_array(i)-d1y)^2);
    calc_R2 = sqrt((x_array(i)-d2x)^2 + (y_array(i)-d2y)^2);
    calc_R3 = sqrt((x_array(i)-d3x)^2 + (y_array(i)-d3y)^2);
    calc_R4 = sqrt((x_array(i)-d4x)^2 + (y_array(i)-d4y)^2);

    bool1 = abs(R1_array(i) - calc_R1) < 1e-11;
    bool2 = abs(R2_array(i) - calc_R2) < 1e-11;
    bool3 = abs(R3_array(i) - calc_R3) < 1e-11;
    bool4 = abs(R4_array(i) - calc_R4) < 1e-11;

    if bool1 && bool2 && bool3 && bool4 && w_array(i) > 330 && x_array(i) > 0 && y_array(i) > 0 && x_array(i) < x_max && y_array(i) < y_max
        count = count + 1;
        correct_index = i;
    end
end

% Correct values for x, y, and w
x_correct = x_array(correct_index);
y_correct = y_array(correct_index);
w_correct = w_array(correct_index);

fprintf('x: %.3f m y: %.3f m w: %.2f m/s\n', x_correct, y_correct, w_correct);
